#!/usr/bin/env python3
"""tests/net/proxy.py LOG [--deny HOST]... -- an HTTP proxy for the tests on
127.0.0.1, on a free port, which it prints first. CONNECT HOST:PORT opens a tunnel
and relays both ways (403 for a denied HOST); a GET in absolute form is sent on to
its server and the answer relayed (localhost and the names under it reached at
127.0.0.1, as browsers resolve them). Each request's method and target is appended to
LOG, with its Proxy-Authorization's credentials when it has them."""
import base64
import select
import socket
import socketserver
import sys
import threading

LOG = sys.argv[1]
DENY = [sys.argv[i + 1] for i in range(2, len(sys.argv) - 1) if sys.argv[i] == "--deny"]
lock = threading.Lock()


def log(line):
    with lock, open(LOG, "a") as f:
        f.write(line + "\n")


def relay(a, b):
    socks = [a, b]
    while True:
        r, _, _ = select.select(socks, [], [], 10)
        if not r:
            return
        for s in r:
            data = s.recv(65536)
            if not data:
                return
            (b if s is a else a).sendall(data)


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline().decode("latin-1").rstrip("\r\n")
        headers = []
        while True:
            h = self.rfile.readline().decode("latin-1")
            if h in ("\r\n", "\n", ""):
                break
            headers.append(h.rstrip("\r\n"))
        parts = line.split(" ")
        if len(parts) != 3:
            return
        method, target = parts[0], parts[1]
        auth = [h.split(":", 1)[1].strip() for h in headers if h.lower().startswith("proxy-authorization:")]
        note = ""
        if auth and auth[0].startswith("Basic "):
            note = " as " + base64.b64decode(auth[0][6:]).decode("latin-1")
        log(method + " " + target + note)
        if method == "CONNECT":
            host, _, port = target.rpartition(":")
            if host.strip("[]") in DENY:
                self.wfile.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
                return
            # localhost and the names under it are the loopback address, as a browser resolves them
            name = host.strip("[]")
            if name.lower().rstrip(".") == "localhost" or name.lower().rstrip(".").endswith(".localhost"):
                name = "127.0.0.1"
            try:
                up = socket.create_connection((name, int(port)), timeout=10)
            except OSError:
                self.wfile.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n")
                return
            self.wfile.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
            self.wfile.flush()
            with up:
                relay(self.connection, up)
            return
        if not target.startswith("http://"):
            self.wfile.write(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            return
        rest = target[len("http://"):]
        authority, slash, path = rest.partition("/")
        host, _, port = authority.rpartition(":") if ":" in authority else (authority, "", "80")
        kept = [h for h in headers if not h.lower().startswith(("proxy-", "connection:"))]
        req = "%s /%s HTTP/1.1\r\n%s\r\nConnection: close\r\n\r\n" % (method, path, "\r\n".join(kept))
        with socket.create_connection((host, int(port)), timeout=10) as up:
            up.sendall(req.encode("latin-1"))
            while True:
                data = up.recv(65536)
                if not data:
                    break
                self.wfile.write(data)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


with Server(("127.0.0.1", 0), Handler) as srv:
    print(srv.server_address[1], flush=True)
    srv.serve_forever()
