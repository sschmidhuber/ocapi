#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025 ocapi Contributors
#
# Minimal mock of an Ollama server (https://github.com/ollama/ollama): enough
# of the API for ocapi's "Test connection", chat streaming and tool calling to
# work.
#
#   GET  /api/tags        -> {"models": [{"name": "mock-model"}, ...]}
#   POST /api/chat        -> newline-delimited JSON (Ollama's actual stream
#                            format, one JSON object per line):
#                            {"message": {"role": "assistant", "content": ...}}
#                            ... final chunk with "done": true
#
# Tool calling: when tools were offered in the request and the last user
# message contains the word "tool", the mock answers with a tool_calls
# chunk (Ollama format) requesting the "echo" tool of the seeded "Mock MCP"
# server; the next round (with the tool result in the conversation) streams
# a plain text reply again.
#
# Reasoning simulation: when the last user message contains the word
# "reasoning", the mock answers the way reasoning-capable models stream
# through OpenAI-compatible surfaces (as observed from Z.ai's GLM models):
# first "thinking" content parts, then the first visible token as a
# {"type":"text"} content part array, and the rest as plain string content.
# ocapi must keep the visible text and drop the reasoning.
#
# Anything else returns 404. No dependencies beyond the Python standard
# library; used only for local tests and CI end-to-end runs.

import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MODEL_NAME = "mock-model"
REPLY_TEXT = "Hello from the mock backend!"
REASONING_REPLY_TEXT = "Hello from the reasoning mock! I thought about it first."
THINKING_TEXT = "The user greets me, so I should greet back."
TOOL_REPLY_TEXT = "The tool says: echo: Hello from the MCP tool!"
TOOL_NAME = "Mock MCP__echo"
TOOL_TRIGGER = "use the tool"
REASONING_TRIGGER = "reasoning"


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
        request = json.loads(self.rfile.read(length) or b"{}")

        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        def send_event(payload):
            event = json.dumps(payload) + "\n"
            self._write_chunk(event.encode())

        # A tool result already in the conversation means the tool round is
        # done: answer with text. This must win over the trigger check, since
        # the triggering user message is still the last one.
        if self._in_tool_round(request):
            reply = TOOL_REPLY_TEXT
        elif self._wants_tool(request):
            send_event(
                {
                    "model": MODEL_NAME,
                    "message": {
                        "role": "assistant",
                        "content": "",
                        "tool_calls": [
                            {
                                "function": {
                                    "name": TOOL_NAME,
                                    "arguments": {"text": "Hello from the MCP tool!"},
                                }
                            }
                        ],
                    },
                    "done": False,
                }
            )
            send_event(
                {"model": MODEL_NAME, "message": {"role": "assistant", "content": ""}, "done": True}
            )
            self.wfile.write(b"0\r\n\r\n")
            return
        elif self._wants_reasoning(request):
            self._stream_reasoning_reply(send_event)
            return
        else:
            reply = REPLY_TEXT
        for token in reply.split(" "):
            send_event(
                {"model": MODEL_NAME, "message": {"role": "assistant", "content": token + " "}}
            )
            time.sleep(0.05)

        send_event(
            {"model": MODEL_NAME, "message": {"role": "assistant", "content": ""}, "done": True}
        )
        self.wfile.write(b"0\r\n\r\n")

    def _stream_reasoning_reply(self, send_event):
        # Wire shape of a reasoning model: thinking parts first, then the
        # first visible token as a content-parts array, then plain strings.
        send_event(
            {"model": MODEL_NAME, "message": {"role": "assistant", "content": ""}, "done": False}
        )
        for segment in THINKING_TEXT.split(" "):
            send_event(
                {
                    "model": MODEL_NAME,
                    "message": {
                        "role": "assistant",
                        "content": [
                            {
                                "type": "thinking",
                                "thinking": [{"type": "text", "text": segment + " "}],
                                "closed": True,
                            }
                        ],
                    },
                    "done": False,
                }
            )
            time.sleep(0.05)
        tokens = REASONING_REPLY_TEXT.split(" ")
        # First visible token as a typed content-part array.
        send_event(
            {
                "model": MODEL_NAME,
                "message": {"role": "assistant", "content": [{"type": "text", "text": tokens[0]}]},
                "done": False,
            }
        )
        time.sleep(0.05)
        # Remaining tokens as plain string content.
        for token in tokens[1:]:
            send_event(
                {
                    "model": MODEL_NAME,
                    "message": {"role": "assistant", "content": " " + token},
                    "done": False,
                }
            )
            time.sleep(0.05)
        send_event(
            {"model": MODEL_NAME, "message": {"role": "assistant", "content": ""}, "done": True}
        )
        self.wfile.write(b"0\r\n\r\n")

    @staticmethod
    def _wants_reasoning(request):
        return REASONING_TRIGGER in OllamaMockHandler._last_user_message(request).lower()

    @staticmethod
    def _last_user_message(request):
        for message in reversed(request.get("messages") or []):
            if message.get("role") == "user":
                return message.get("content") or ""
        return ""

    def _wants_tool(self, request):
        # Offered tools plus the trigger word in the last user message make
        # the mock act like a model deciding to call a tool.
        tools = request.get("tools") or []
        if not tools:
            return False
        return TOOL_TRIGGER in self._last_user_message(request).lower()

    @staticmethod
    def _in_tool_round(request):
        # A tool result message in the conversation means the tool round is
        # done; answer with text summarizing it.
        return any(message.get("role") == "tool" for message in request.get("messages") or [])

    def _write_chunk(self, data):
        self.wfile.write(f"{len(data):x}\r\n".encode() + data + b"\r\n")
        self.wfile.flush()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 11434
    server = ThreadingHTTPServer(("127.0.0.1", port), OllamaMockHandler)
    print(f"[mock-ollama] listening on http://127.0.0.1:{port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
