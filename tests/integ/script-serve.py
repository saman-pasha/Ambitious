#!/usr/bin/env python3
"""tests/integ/script-serve.py [--log FILE] ROOT... -- the script gates' site over HTTP/1.1
on 127.0.0.1, on a free port, which it prints first. A page at http://localhost:PORT/ is
the same site from another origin.

Each ROOT's files, the first that has the path, typed by their extension; in an .html
file PORT is replaced by the server's port. Besides the files, for any method:
  /echo...           200, text/plain: the request's method and path, its Content-Type,
                     then its body
  /status/NNN        that status, HTTP's reason phrase for it, a short body
  /redirect/PATH     302 to /PATH
  /cross/PATH        302 to /PATH at http://localhost:PORT
--log appends a line a request: its method, its path, and its body when it has one, its
CR and LF written \r and \n."""
import argparse
import http.server
import os
import socketserver
import sys
import threading

ap = argparse.ArgumentParser()
ap.add_argument("--log")
ap.add_argument("roots", nargs="+")
args = ap.parse_args()
ROOTS = [os.path.abspath(r) for r in args.roots]
LOCK = threading.Lock()
TYPES = {".html": "text/html", ".js": "text/javascript", ".json": "application/json", ".txt": "text/plain",
         ".css": "text/css", ".png": "image/png", ".svg": "image/svg+xml"}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def reply(self, status, ctype, body, extra=()):
        self.send_response(status)
        if ctype:
            self.send_header("Content-Type", ctype)
        for k, v in extra:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def handle_any(self):
        sent = self.body()
        if args.log:
            with LOCK, open(args.log, "a") as f:
                body = sent.decode("utf-8", "replace").replace("\r", "\\r").replace("\n", "\\n")
                f.write("%s %s%s\n" % (self.command, self.path, (" | " + body) if sent else ""))
        port = self.server.server_address[1]
        path = self.path.split("#")[0]
        if path.startswith("/echo"):
            text = "%s %s\ncontent-type: %s\n\n" % (self.command, path, self.headers.get("Content-Type") or "")
            return self.reply(200, "text/plain", text.encode() + sent)
        if path.startswith("/status/"):
            code = int(path[8:11])
            return self.reply(code, "text/plain", ("status %d" % code).encode())
        if path.startswith("/redirect/"):
            return self.reply(302, "text/plain", b"moved", [("Location", "/" + path[10:])])
        if path.startswith("/cross/"):
            return self.reply(302, "text/plain", b"moved", [("Location", "http://localhost:%d/%s" % (port, path[7:]))])
        rel = path.split("?")[0].lstrip("/") or "index.html"
        for root in ROOTS:
            full = os.path.normpath(os.path.join(root, rel))
            if full.startswith(root) and os.path.isfile(full):
                with open(full, "rb") as f:
                    data = f.read()
                ext = os.path.splitext(full)[1]
                if ext == ".html":
                    data = data.replace(b"PORT", str(port).encode())
                return self.reply(200, TYPES.get(ext, "application/octet-stream"), data)
        return self.reply(404, "text/plain", b"not found")

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_HEAD = do_OPTIONS = handle_any


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


srv = Server(("127.0.0.1", 0), Handler)
sys.stdout.write("%d\n" % srv.server_address[1])
sys.stdout.flush()
srv.serve_forever()
