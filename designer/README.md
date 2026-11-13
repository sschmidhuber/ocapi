# Qt Designer UI files

The main window layout is loaded at runtime from **`src/ui/mainwindow.ui`**
(CMake AUTOUIC generates `ui_mainwindow.h`; `MainWindow::setupUi()` calls
`Ui::MainWindow::setupUi(this)`). Open it in Qt Widget Designer:

```sh
designer src/ui/mainwindow.ui        # or open via Qt Creator
```

Custom widgets are promoted widgets in the .ui file:

| Widget in .ui | Real class |
|---|---|
| `sidebar` | `okapi::Sidebar` (`ui/sidebar.h`) |
| `chatView` | `okapi::WebChatView` (`ui/webchatview.h`) |
| `composer` | `okapi::Composer` (`ui/composer.h`) |
| `modelPicker` | `okapi::ModelPickerButton` (`ui/mainwindow.h`) |

Edit the layout in Designer, keep `objectName`s stable (unit + E2E tests
locate widgets by name/accessible name), and rebuild.
