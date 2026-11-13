# Designer UI representation

`mainwindow.ui` is a **Qt Widget Designer** representation of Okapi's main
window (toolbar, sidebar, chat area, composer). Open it with:

```sh
designer designer/mainwindow.ui        # or open via Qt Creator
```

Note: Okapi builds its UI programmatically in `src/ui/mainwindow.cpp`
(`MainWindow::setupUi()`); the app does **not** load this file at runtime.
Custom widgets are represented by placeholders:

| Placeholder in .ui | Real class in code |
|---|---|
| `sidebar` / `sidebarTree` | `Sidebar` (contains a `QTreeWidget`) |
| `chatView` | `WebChatView` (`QWebEngineView`) |
| `composer` / `composerInput` | `Composer` / `MessageTextEdit` (`QPlainTextEdit`) |
| `modelPicker` | `ModelPickerButton` |

Use this file to experiment with the layout in Designer; port changes back
to `setupUi()` in code.
