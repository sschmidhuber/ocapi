# ocapi

A desktop chat client for agentic work. It talks to local inference servers
(Ollama, llama.cpp, vLLM) and cloud APIs (OpenAI, Anthropic, Google Gemini,
Mistral AI, and any OpenAI-compatible endpoint) — locally or on remote
machines.

- C++23, Qt 6 (Widgets + Network)
- Streaming responses over Server-Sent Events
- KDE Plasma look & feel, works on GNOME and other desktops
- Free desktop standards (XDG desktop entry, QSettings for configuration)

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

## Building

Requirements: CMake ≥ 3.21, a C++23 compiler (GCC 13+, Clang 17+), Qt 6.5+
(Widgets, Network).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/ocapi
```

Install:

```sh
cmake --install build   # or: sudo cmake --install build --prefix /usr
```

## Usage

1. Start the app, click **Manage backends…** in the sidebar.
2. Add a backend: pick a type, set the base URL (remote hosts work, e.g.
   `http://myserver:11434`) and paste an API key for cloud services.
3. Back in the main window, pick the backend and model, then chat.
   Responses stream in token by token; press **Stop** to cancel.

API keys and backend settings are stored via `QSettings`
(`~/.config/sschmidhuber/ocapi.conf`).

## Project layout

```
src/
  main.cpp                 application entry point
  core/                    messages, chat sessions, backend configuration + registry
  backends/                IBackend interface, SSE client, one class per provider family
  ui/                      main window, sidebar, chat view/delegate, composer, backend dialog
resources/icons/ocapi.svg  application icon
data/                      XDG desktop entry
```

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
