#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025 ocapi Contributors
#
# Minimal mock of an Ollama server (https://github.com/ollama/ollama): enough
# of the API for ocapi's "Test connection" and chat streaming to work.
#
#   GET  /api/tags        -> {"models": [{"name": "mock-model"}, ...]}
#   POST /api/chat        -> Server-Sent Events (ocapi consumes the stream
#                            through its SSE client):
#                            data: {"message": {"role": "assistant", "content": ...}}
#                            ... final chunk with "done": true
#
# Anything else returns 404. No dependencies beyond the Python standard
# library; used only for local tests and CI end-to-end runs.

import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

MODEL_NAME = "mock-model"
REPLY_TEXT = "Hello from the mock backend!"


class OllamaMockHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[mock-ollama] " + fmt % args + "\n")

    def _send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/tags":
            self._send_json({"models": [{"name": MODEL_NAME}]})
        else:
            self._send_json({"error": "not found"}, 404)

    def do_POST(self):
        if self.path != "/api/chat":
            self._send_json({"error": "not found"}, 404)
            return

        length = int(self.headers.get("Content-Length", 0))
        self.rfile.read(length)

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def send_event(payload):
            event = "data: " + json.dumps(payload) + "\n\n"
            self._write_chunk(event.encode())

        for token in REPLY_TEXT.split(" "):
            send_event({"model": MODEL_NAME,
                        "message": {"role": "assistant", "content": token + " "}})
            time.sleep(0.05)

        send_event({"model": MODEL_NAME,
                    "message": {"role": "assistant", "content": ""},
                    "done": True})
        self.wfile.write(b"0\r\n\r\n")

    def _write_chunk(self, data):
        self.wfile.write(("%x\r\n" % len(data)).encode() + data + b"\r\n")
        self.wfile.flush()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 11434
    server = HTTPServer(("127.0.0.1", port), OllamaMockHandler)
    print(f"[mock-ollama] listening on http://127.0.0.1:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
