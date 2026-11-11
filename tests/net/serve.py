#!/usr/bin/env python3
"""tests/net/serve.py [--tls CERT KEY] [--tls12] [--sub NAME=VALUE]... [--date EPOCH] [--log FILE] ROOT...
-- the test site over HTTP/1.1 on 127.0.0.1, on a free port, which it prints first.

Each ROOT's files, the first that has the path; in an .html file each NAME of --sub
is replaced by its VALUE, then PORT by the server's port. A file's FILE.headers, if
there is one, holds header lines its response adds (Set-Cookie, Cache-Control ...);
an Expires or Last-Modified written +N or -N is the response's Date plus or less N
seconds, and an ETag written auto is a hash of the body. A request whose
If-None-Match holds the file's ETag, or else whose If-Modified-Since is not before
its Last-Modified, is answered 304 with those headers, or with FILE.304.headers'
when there is one. A Content-Encoding there is done to the body, coding by coding
in its order, by Python's gzip and zlib: gzip, deflate (zlib's stream), and as
names of this server's own deflate-raw (deflate data with no zlib header, sent as
deflate) and gzip-bad (gzip with a wrong CRC-32, sent as gzip); another coding
(br) is named and not done. Besides the files: /old.css moved (301) to
/moved/new.css, which moved (302, relatively) to /style2.css; /loop.css redirects
to itself; /chunked.css comes in three chunks.

--tls serves HTTPS with the certificate chain CERT and its key KEY, offering
http/1.1 by ALPN; --tls12 holds it to TLS 1.2. --date fixes every response's Date
(its clock, for the cache's ages). --log appends a line for each request: its host,
its path, and the Cookie and validators it carried; --log-accept adds its
Accept-Encoding."""
import argparse
import email.utils
import gzip
import hashlib
import http.server
import os
import socketserver
import ssl
import sys
import threading
import time
import zlib

ap = argparse.ArgumentParser()
ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
ap.add_argument("--tls12", action="store_true")
ap.add_argument("--sub", action="append", default=[])
ap.add_argument("--date", type=int)
ap.add_argument("--log")
ap.add_argument("--log-accept", action="store_true")
ap.add_argument("roots", nargs="+")
args = ap.parse_args()
ROOTS = [os.path.abspath(r) for r in args.roots]
SUBS = [s.split("=", 1) for s in args.sub]
LOCK = threading.Lock()


def now():
    return args.date if args.date is not None else int(time.time())


def log(handler):
    if not args.log:
        return
    host = (handler.headers.get("Host") or "").split(":")[0]
    line = "%s %s %s" % (handler.command, host, handler.path)
    for name in ("Cookie", "If-None-Match", "If-Modified-Since") + (("Accept-Encoding",) if args.log_accept else ()):
        v = handler.headers.get(name)
        if v is not None:
            line += " | %s: %s" % (name.lower(), v)
    with LOCK, open(args.log, "a") as f:
        f.write(line + "\n")


def sidecar(path, body):
    """the header lines of a FILE.headers, relative dates and auto ETags made"""
    out = []
    if os.path.isfile(path):
        with open(path) as f:
            for line in f:
                if ":" not in line:
                    continue
                k, v = (x.strip() for x in line.rstrip("\n").split(":", 1))
                if k.lower() in ("expires", "last-modified") and v[:1] in "+-" and v[1:].isdigit():
                    v = email.utils.formatdate(now() + int(v), usegmt=True)
                if k.lower() == "etag" and v == "auto":
                    v = '"%s"' % hashlib.sha1(body).hexdigest()[:12]
                out.append((k, v))
    return out


def encode(headers, body):
    """the body in the codings of its Content-Encoding, in their order, and the headers to send"""
    out = []
    for k, v in headers:
        if k.lower() != "content-encoding":
            out.append((k, v))
            continue
        names = []
        for c in (x.strip() for x in v.split(",")):
            if c == "gzip":
                body = gzip.compress(body, mtime=0)
            elif c == "gzip-bad":
                body = gzip.compress(body, mtime=0)
                body = body[:-8] + bytes([body[-8] ^ 0xFF]) + body[-7:]
                c = "gzip"
            elif c == "deflate":
                body = zlib.compress(body)
            elif c == "deflate-raw":
                z = zlib.compressobj(wbits=-15)
                body = z.compress(body) + z.flush()
                c = "deflate"
            names.append(c)
        out.append((k, ", ".join(names)))
    return out, body


def fresh_for(handler, headers):
    """whether the request's validators match the response's: If-None-Match first"""
    etag = next((v for k, v in headers if k.lower() == "etag"), None)
    inm = handler.headers.get("If-None-Match")
    if inm is not None:
        tags = [t.strip() for t in inm.split(",")]
        return etag is not None and (etag in tags or "W/" + etag in tags or "*" in tags)
    lm = next((v for k, v in headers if k.lower() == "last-modified"), None)
    ims = handler.headers.get("If-Modified-Since")
    if ims is not None and lm is not None:
        try:
            return email.utils.parsedate_to_datetime(ims) >= email.utils.parsedate_to_datetime(lm)
        except (TypeError, ValueError):
            return False
    return False


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def date_time_string(self, timestamp=None):
        return email.utils.formatdate(now(), usegmt=True)

    def plain(self, code, headers, body=b""):
        self.send_response(code)
        for k, v in headers:
            self.send_header(k, v)
        if code != 304:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if code != 304:
            self.wfile.write(body)

    def do_GET(self):
        port = self.server.server_address[1]
        log(self)
        if self.path == "/old.css":
            return self.plain(301, [("Location", "/moved/new.css")])
        if self.path == "/moved/new.css":
            return self.plain(302, [("Location", "../style2.css")])
        if self.path == "/loop.css":
            return self.plain(302, [("Location", "/loop.css")])
        if self.path == "/chunked.css":
            body = b".chunked { color: green; font-weight: bold }"
            self.send_response(200)
            self.send_header("Content-Type", "text/css")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (body[:12], body[12:30], body[30:]):
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            return
        # the roots' files, with this server's own types and 404, the same whatever Python serves
        rel = self.path.split("?")[0].lstrip("/")
        for root in ROOTS:
            p = os.path.realpath(os.path.join(root, rel))
            if p.startswith(root + os.sep) and os.path.isfile(p):
                break
        else:
            return self.plain(404, [("Content-Type", "text/plain")], b"not found\n")
        with open(p, "rb") as f:
            body = f.read()
        ext = os.path.splitext(p)[1]
        if ext == ".html":
            for name, value in SUBS:
                body = body.replace(name.encode(), value.encode())
            body = body.replace(b"PORT", str(port).encode())
        types = {".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript", ".png": "image/png"}
        headers = [("Content-Type", types.get(ext, "application/octet-stream"))] + sidecar(p + ".headers", body)
        if fresh_for(self, headers):
            if os.path.isfile(p + ".304.headers"):
                headers = sidecar(p + ".304.headers", body)
            return self.plain(304, [(k, v) for k, v in headers if k.lower() not in ("content-type", "content-encoding")])
        headers, body = encode(headers, body)
        return self.plain(200, headers, body)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def get_request(self):
        sock, addr = super().get_request()
        if TLS is None:
            return sock, addr
        # the handshake in the handler's thread, so that one that fails stops no other
        return TLS.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), addr

    def finish_request(self, request, client_address):
        if TLS is not None:
            try:
                request.do_handshake()
            except (ssl.SSLError, OSError):
                return
        super().finish_request(request, client_address)

    def handle_error(self, request, client_address):
        pass


TLS = None
if args.tls:
    TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    TLS.load_cert_chain(*args.tls)
    TLS.set_alpn_protocols(["http/1.1"])
    if args.tls12:
        TLS.maximum_version = ssl.TLSVersion.TLSv1_2

with Server(("127.0.0.1", 0), Handler) as srv:
    print(srv.server_address[1], flush=True)
    srv.serve_forever()
