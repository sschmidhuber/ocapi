#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025 ocapi Contributors
#
# Minimal mock of an MCP server (Model Context Protocol, streamable HTTP
# transport): enough of the protocol for ocapi's MCP client to connect,
# list tools and call them. Every POST is answered with plain JSON (one of
# the response shapes ocapi's McpClient accepts).
#
#   POST /  initialize             -> capabilities + serverInfo
#   POST /  notifications/initial… -> 202
#   POST /  tools/list             -> one tool: echo(text) -> "echo: <text>"
#   POST /  resources/list, resources/templates/list, prompts/list -> empty
#   POST /  tools/call             -> {"content": [{"type": "text", …}]}
#
# The echo tool is what the E2E chat test drives: the mock Ollama server
# asks the model to call it when the user message contains "tool".
#
# No dependencies beyond the Python standard library.

import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOOL_RESULT_PREFIX = "echo: "


class McpMockHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[mock-mcp] " + fmt % args + "\n")

    def _send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        message = json.loads(self.rfile.read(length) or b"{}")

        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params") or {}

        if method == "initialize":
            self._send_json({
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {
                    "protocolVersion": "2025-06-18",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "mock-mcp", "version": "0.1.0"},
                },
            })
            return
        if method == "notifications/initialized":
            self.send_response(202)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if method == "tools/list":
            self._send_json({
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {"tools": [{
                    "name": "echo",
                    "description": "Echo the given text back to the caller",
                    "inputSchema": {
                        "type": "object",
                        "properties": {"text": {"type": "string"}},
                        "required": ["text"],
                    },
                }]},
            })
            return
        if method == "tools/call":
            arguments = params.get("arguments") or {}
            text = arguments.get("text", "")
            self._send_json({
                "jsonrpc": "2.0",
                "id": request_id,
                "result": {
                    "content": [{"type": "text", "text": TOOL_RESULT_PREFIX + text}],
                    "isError": False,
                },
            })
            return
        if method in ("resources/list", "resources/templates/list", "prompts/list"):
            key = {"resources/list": "resources",
                   "resources/templates/list": "resourceTemplates",
                   "prompts/list": "prompts"}[method]
            self._send_json({"jsonrpc": "2.0", "id": request_id, "result": {key: []}})
            return

        if request_id is not None:
            self._send_json({
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": -32601, "message": "method not found: %s" % method},
            })
            return
        self.send_response(202)
        self.send_header("Content-Length", "0")
        self.end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 11435
    server = ThreadingHTTPServer(("127.0.0.1", port), McpMockHandler)
    print(f"[mock-mcp] listening on http://127.0.0.1:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
