#!/usr/bin/env python3
"""Link Ziran's curl transport and exercise it on a disposable loopback server."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import threading


ROOT = Path(__file__).resolve().parent.parent
COMPILER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/bin/zi2c"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, status, body):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/ok":
            self.respond(200, b"ready")
        elif self.path == "/auth":
            self.respond(401, b"denied")
        elif self.path == "/large":
            self.respond(200, b"x" * 256)
        else:
            self.respond(404, b"missing")

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(size)
        if (self.path == "/echo" and
                self.headers.get("Authorization") == "Bearer secret" and
                self.headers.get("Accept") == "application/json" and
                self.headers.get("Content-Type") == "application/json" and
                self.headers.get("X-Daochi-User") == "account" and
                body == b'{"value":42}'):
            self.respond(201, b"accepted")
        else:
            self.respond(400, b"invalid")


with tempfile.TemporaryDirectory(prefix="ziran-curl-") as temporary:
    generated = Path(temporary) / "generated"
    executable = Path(temporary) / "client"
    subprocess.run([
        str(COMPILER), "--no-main", "--root", str(ROOT / "std"),
        "--module-path", str(ROOT / "std"), "-o", str(generated),
        str(ROOT / "std/net_http_curl_linux.zi"),
    ], check=True)
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-O0", "-Wall", "-Wextra",
        "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
        f"-I{ROOT / 'include'}", f"-I{generated}",
        str(ROOT / "tests/net_http_curl_linux_client.c"),
        str(generated / "net_http_curl_linux.c"),
        str(generated / "c_string.c"),
        str(generated / "byte_text_linux.c"),
        "-l:libcurl.so.4", "-o", str(executable),
    ], check=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        environment = os.environ.copy()
        environment.pop("DISPLAY", None)
        environment.pop("WAYLAND_DISPLAY", None)
        subprocess.run([
            str(executable), f"http://127.0.0.1:{server.server_port}"
        ], check=True, env=environment)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
