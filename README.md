# ocapi

A desktop chat client for agentic work. It talks to local inference servers
(Ollama, llama.cpp, vLLM) and cloud APIs (OpenAI, Anthropic, Google Gemini,
Mistral AI, and any OpenAI-compatible endpoint) — locally or on remote
machines.

- C++23, Qt 6 (Widgets + Network)
- Streaming responses over Server-Sent Events
- KDE Plasma look & feel, works on GNOME and other desktops
- Free desktop standards (XDG desktop entry, SQLite storage in the XDG data dir)

## Supported backends

| Type | Protocol | Default endpoint |
|---|---|---|
| Ollama | `/api/chat`, `/api/tags` | `http://127.0.0.1:11434` |
| llama.cpp (`llama-server`) | OpenAI-compatible | `http://127.0.0.1:8080` |
| vLLM | OpenAI-compatible | `http://127.0.0.1:8000/v1` |
| OpenAI | OpenAI-compatible | `https://api.openai.com/v1` |
| Anthropic (Claude) | Messages API | `https://api.anthropic.com/v1` |
| Google Gemini | generateContent API | `https://generativelanguage.googleapis.com/v1beta` |
| Mistral AI | OpenAI-compatible | `https://api.mistral.ai/v1` |

Any OpenAI-compatible server (LM Studio, LocalAI, text-generation-webui with
the OpenAI extension, …) can be added as an "OpenAI" backend by setting its
base URL — including remote machines, e.g. `http://192.168.1.10:8000/v1`.

## Speech-to-text (dictation)

ocapi can transcribe speech to text so chat messages can be dictated instead
of typed. Open **Tools → Speech-to-text…** to configure transcription
services — analogous to the model backend configuration:

- **Services**: OpenAI transcription API, Groq whisper models, Mistral AI
  audio transcription, and any OpenAI-compatible transcription server
  (faster-whisper-server, Speaches, LocalAI, …) — local or remote. The
  whisper.cpp server (`whisper-server`) is supported as well.
- **Live transcription**: while you speak, already-spoken words appear in
  the message input; press the microphone button in the composer to start
  and stop dictation.
- **Audio input device**: choose the microphone in the settings.
- **Test**: the settings page can send an audio file to a configured service
  to verify the connection and credentials.

## Text-to-speech (read aloud)

ocapi can speak assistant responses aloud so you do not have to read
everything. Open **Tools → Text-to-speech…** to configure speech services —
analogous to the model backend configuration — and enable **Speak assistant
responses aloud**; the 🔊 button in the chat footer toggles it per session.

- **Services**: the speech APIs of the LLM providers (OpenAI, Groq,
  Mistral AI), ElevenLabs, any OpenAI-compatible TTS server (local or
  remote), and a Piper HTTP server (`rhasspy/piper`).
- **Low latency**: text is split into sentences while it streams in; each
  completed sentence is synthesized immediately and playback starts as soon
  as the first sentence is ready — before the full response has arrived.
  Markdown and HTML markup (code blocks, links, emphasis, …) is stripped
  before speaking.
- **Audio output device**: choose the output device in the settings.
- **Test**: the settings page can synthesize a sample sentence to verify
  the connection, credentials and chosen voice.

## MCP servers (Model Context Protocol)

ocapi can connect to [MCP](https://modelcontextprotocol.io) servers and make
their **tools**, **resources** and **prompts** available to the model. Open
**Tools → MCP servers…** in the sidebar to configure servers:

- **Transports**: Streamable HTTP (MCP 2025-03-26+, JSON or SSE responses),
  legacy HTTP+SSE, and **stdio** (spawn a local server process, e.g.
  `npx -y @modelcontextprotocol/server-filesystem /tmp`).
- **Connection test**: the dialog runs the MCP `initialize` handshake and
  lists the server's tools, resources and prompts with their capabilities.
- **Permissions**: per tool and per server you choose **Off** (disabled),
  **Ask** (ocapi asks before every use) or **Allow** (no confirmation). The
  server-wide default applies to tools without an override.
- **Chat integration**: when the model calls a tool, the call and its result
  are shown in the chat; ocapi then feeds the result back to the model and
  continues the conversation automatically.

MCP works with any backend that supports OpenAI-style function calling
(OpenAI-compatible servers such as Ollama, llama.cpp and vLLM). The MCP
client speaks the full protocol: tools, resources (list/read/templates),
prompts (list/get), logging notifications and capability negotiation.

MCP server configurations are stored in the same SQLite database as backends
and chats.

## Attachments

Besides images (JPEG, PNG, WebP, JPEG XL, AVIF, TIFF, GIF), PDF documents,
office documents (.docx, .xlsx, .pptx MS Office and .odt, .ods, .odp
OpenDocument), archive files (.zip, .tar.gz, … — contents are extracted
per entry, with entry-count and size limits as protection against
decompression bombs), e-mail messages (.eml, .mbox) and any kind of text
file (CSV, plaintext, Markdown, JSON, logs, source code, …) can be
attached to chat messages. HTML files are converted to readable text
(script/style blocks removed, block structure preserved). PDF text is extracted with Poppler; scanned pages and image files
are recognized with Tesseract OCR; Office documents are unpacked with
libarchive and their XML text runs are read with QXmlStreamReader. Text files
are detected by content (UTF-8, printable characters) regardless of their
extension. Poppler, Tesseract and libarchive are optional at build time
(detected via pkg-config) — the features are disabled gracefully when they
are missing.

Install the dependencies, e.g. on Debian/Ubuntu:

```sh
sudo apt install libpoppler-cpp-dev libtesseract-dev tesseract-ocr-eng libarchive-dev
```

## Building

Requirements: CMake ≥ 3.21, a C++23 compiler (GCC 13+, Clang 17+), Qt 6.5+
(Widgets, Network, Test for the unit tests).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/ocapi
```

Install:

```sh
cmake --install build   # or: sudo cmake --install build --prefix /usr
```

> **AI agent sandboxes** (no root, no system Qt): see
> [docs/sandbox-setup.md](docs/sandbox-setup.md) for a step-by-step guide to
> install Qt 6.8.2 and all build dependencies into a user-writable prefix.

## Packaging

Scripts to build installable packages live in [packaging/](packaging/README.md):

- **Linux**: Flatpak bundle (`packaging/flatpak/build-flatpak.sh`),
  RPM (`packaging/rpm/build-rpm.sh`), DEB (`packaging/deb/build-deb.sh`)
- **Windows**: MSIX (`packaging/windows/build-msix.ps1`), MSI via WiX
  (`packaging/windows/build-msi.ps1`), EXE installer via NSIS
  (`packaging/windows/build-exe.ps1`)

Packages are built unsigned; see [packaging/README.md](packaging/README.md)
for how to sign them for distribution.

## Testing

Unit tests use Qt Test and live in `tests/`. They cover the core domain
models (`Message`, `ChatSession`, `BackendConfig`, `BackendRegistry`), the
backend factory, and the SSE stream parser.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

To build without tests: `cmake -B build -DOCAPI_BUILD_TESTS=OFF`.

## Usage

1. Start the app, click **Tools → Model backends…** in the sidebar.
2. Add a backend: pick a type, set the base URL (remote hosts work, e.g.
   `http://myserver:11434`) and paste an API key for cloud services.
3. Back in the main window, pick the backend and model, then chat.
   Responses stream in token by token; press **Stop** to cancel.

Backend settings, API keys and chat history are stored in an SQLite database
(`$XDG_DATA_HOME/sschmidhuber/ocapi/ocapi.sqlite`).

## Project layout

```
src/
  main.cpp                 application entry point
  core/                    messages, chat sessions, backend + MCP configuration and registries
  backends/                IBackend interface, SSE client/parser, one class per provider
                           family, MCP client (stdio + HTTP transports)
  ui/                      main window, sidebar, chat view/delegate, composer, backend and
                           MCP server dialogs
tests/                     Qt Test unit tests
resources/icons/ocapi.svg  application icon
data/                      XDG desktop entry
```

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
