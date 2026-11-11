#!/usr/bin/env python3
"""tests/net/serve.py ROOT -- the test site over HTTP/1.1 on 127.0.0.1, on a free port,
which it prints first. Besides ROOT's files: /old.css moved (301) to /moved/new.css,
which moved (302, relatively) to /style2.css; /loop.css redirects to itself;
/chunked.css comes in three chunks; PORT in an .html file is the server's port."""
import http.server
import os
import socketserver
import sys

ROOT = os.path.abspath(sys.argv[1])


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
        # ROOT's files, with this server's own types and 404, the same whatever Python serves
        p = os.path.realpath(os.path.join(ROOT, self.path.split("?")[0].lstrip("/")))
        if not p.startswith(ROOT + os.sep) or not os.path.isfile(p):
            return self.plain(404, [("Content-Type", "text/plain")], b"not found\n")
        with open(p, "rb") as f:
            body = f.read()
        ext = os.path.splitext(p)[1]
        if ext == ".html":
            body = body.replace(b"PORT", str(port).encode())
        types = {".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript", ".png": "image/png"}
        return self.plain(200, [("Content-Type", types.get(ext, "application/octet-stream"))], body)


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler) as srv:
    print(srv.server_address[1], flush=True)
    srv.serve_forever()
