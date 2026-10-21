# End-to-end tests

Essential user flows of the ocapi UI, driven through KDE's
[selenium-webdriver-at-spi](https://github.com/KDE/selenium-webdriver-at-spi)
(the AT-SPI-based Appium/WebDriver stack KDE uses for its own end-to-end
tests). The tests are black-box: they find and interact with widgets through
the Linux accessibility API, never through pixel comparison.

## What is covered

1. **Startup** — the main window comes up with the sidebar (new chat button),
   composer and send button.
2. **Backend setup** — the seeded mock backend is the active backend and its
   `mock-model` was detected through a real `GET /api/tags` round-trip
   against the mock server (visible in the footer's backend/model combos).
3. **Chat** — a message typed into the composer is sent, and the mock
   backend's reply streams back into the chat view.
4. **MCP setup and tool usage** — the seeded mock MCP server connects and
   offers its `echo` tool (status bar shows "MCP: 1 tool(s)"). A chat
   message with a trigger phrase makes the mock backend request the tool;
   ocapi runs it against the mock MCP server, feeds the result back to the
   model, and the final answer (quoting the tool result) appears in the chat.
   This drives the complete agent loop: tool advertisement, model tool call,
   MCP execution, result feedback and the model's follow-up.

Popup menus (Tools → Model backends…) cannot be driven with this stack:
Qt excludes `Qt::Popup` windows such as `QMenu` from the AT-SPI
application tree (`QAccessibleApplication::topLevelObjects` filters
`w->type() != Qt::Popup`), so their items never appear for the driver.
Backend setup is therefore verified through the widgets the flow
populates rather than through the menu.

## Mock backend

[`mock_ollama.py`](mock_ollama.py) is a dependency-free mock of the parts of
the Ollama API ocapi uses:

- `GET /api/tags` — reports a single `mock-model`
- `POST /api/chat` — streams a fixed reply as Server-Sent Events

The test run sets `OCAPI_E2E_MOCK_BACKEND=1`, which makes ocapi pre-seed a
"Mock backend" entry pointing at the mock server
(`src/main.cpp`), so no menu-driven configuration is required for the chat
test. `OCAPI_E2E_MOCK_PORT` selects the port (default 11434, Ollama's
default).

## Running locally

Prerequisites (Debian/Ubuntu package names):

```
sudo apt install at-spi2-core xvfb dbus-x11 python3-pip python3-venv \
     gir1.2-atspi-2.0 gir1.2-gtk-3.0 python3-gi python3-pyatspi
```

Set up the driver (it is not packaged by distributions):

```
git clone https://github.com/KDE/selenium-webdriver-at-spi.git /tmp/swa
python3 -m venv --system-site-packages .venv
. .venv/bin/activate
pip install -r /tmp/swa/requirements.txt
```

Then, from the repository root:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOCAPI_BUILD_TESTS=OFF
cmake --build build

. .venv/bin/activate
export OCAPI_E2E_APP="$PWD/build/src/ocapi"
export QT_QPA_PLATFORM=xcb
python3 tests/e2e/mock_ollama.py 11434 &
(cd /tmp/swa && flask run --port 4723 --no-reload) &
python3 tests/e2e/ocapi_e2e_test.py
```

Inside a desktop session nothing else is needed; on a headless machine wrap
the run in `xvfb-run -a dbus-run-session -- …` and start
`at-spi-bus-launcher --launch-immediately` plus `at-spi2-registryd` first
(this is what the CI job does; see `.github/workflows/ci.yml`).

## CI

The `e2e` job in `.github/workflows/ci.yml` runs the suite on every push to
`main` and on pull requests: it builds ocapi, starts the mock backend, brings
up the AT-SPI bus inside `Xvfb` + `dbus-run-session`, serves the
selenium-webdriver-at-spi WebDriver, and runs `ocapi_e2e_test.py`.
