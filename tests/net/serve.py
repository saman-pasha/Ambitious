#!/usr/bin/env python3
"""tests/net/serve.py [--tls CERT KEY] [--tls12] [--sub NAME=VALUE]... ROOT... -- the
test site over HTTP/1.1 on 127.0.0.1, on a free port, which it prints first.

Each ROOT's files, the first that has the path; in an .html file each NAME of --sub
is replaced by its VALUE, then PORT by the server's port. Besides the files:
/old.css moved (301) to /moved/new.css, which moved (302, relatively) to
/style2.css; /loop.css redirects to itself; /chunked.css comes in three chunks.

--tls serves HTTPS with the certificate chain CERT and its key KEY, offering
http/1.1 by ALPN; --tls12 holds it to TLS 1.2."""
import argparse
import http.server
import os
import socketserver
import ssl
import sys

ap = argparse.ArgumentParser()
ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
ap.add_argument("--tls12", action="store_true")
ap.add_argument("--sub", action="append", default=[])
ap.add_argument("roots", nargs="+")
args = ap.parse_args()
ROOTS = [os.path.abspath(r) for r in args.roots]
SUBS = [s.split("=", 1) for s in args.sub]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def plain(self, code, headers, body=b""):
        self.send_response(code)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        port = self.server.server_address[1]
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
        return self.plain(200, [("Content-Type", types.get(ext, "application/octet-stream"))], body)


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
