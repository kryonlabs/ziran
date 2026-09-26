#!/usr/bin/env python3
"""Exercise Ziran's native WebSocket receiver on a private loopback socket."""
import base64
import hashlib
import http.server
import pathlib
import socketserver
import subprocess
import sys
import tempfile
import threading


ROOT = pathlib.Path(__file__).resolve().parents[1]


class Server(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.headers.get("Authorization") != "Bearer test-token":
            self.send_response(401)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
        ).digest()).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        # A text message in two frames, including a control frame between them.
        self.wfile.write(b'\x01\x05{"hel' + b'\x89\x00' + b'\x80\x06lo":1}')
        self.wfile.flush()

    def log_message(self, *_args):
        pass


class Loopback(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    compiler = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                            ROOT / "build/bin/zi2c")
    with tempfile.TemporaryDirectory() as folder:
        work = pathlib.Path(folder)
        generated = work / "generated"
        subprocess.run([str(compiler), "--no-main", "--root", str(ROOT / "std"),
                        "--module-path", str(ROOT / "std"), "-o", str(generated),
                        str(ROOT / "std/net_ws_curl_linux.zi")], check=True)
        source = work / "test.c"
        source.write_text('''
#include "net_ws_curl_linux.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc == 2);
    char output[256] = {0};
    int32_t status = 0;
    String url = StringView(argv[1], strlen(argv[1]));
    assert(ReceiveWebSocketCurl(NULL, url, StringLiteral("test-token"),
        (Slice){output, sizeof output}, &status));
    assert(status == 200 && strcmp(output, "{\\\"hello\\\":1}") == 0);
    assert(!ReceiveWebSocketCurl(NULL, url, StringLiteral("wrong"),
        (Slice){output, sizeof output}, &status) && status == 401);
    puts("Ziran WebSocket transport passed");
    return 0;
}
''')
        sources = [str(path) for path in generated.glob("*.c")
                   if path.name != "net_http.c"]
        executable = work / "test"
        subprocess.run(["cc", "-std=c11", "-O0", "-I" + str(ROOT / "include"),
                        "-I" + str(generated), str(source), *sources,
                        "-l:libcurl.so.4", "-o", str(executable)], check=True)
        with Loopback(("127.0.0.1", 0), Server) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            url = f"ws://127.0.0.1:{server.server_address[1]}/events"
            subprocess.run([str(executable), url], check=True, timeout=10)
            server.shutdown()


if __name__ == "__main__":
    main()
