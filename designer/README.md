# Qt Designer UI files

The main window and the settings dialog shell are loaded at runtime from
Designer files (CMake AUTOUIC generates `ui_*.h`):

- **`src/ui/mainwindow.ui`** — `MainWindow::setupUi()` calls `Ui::MainWindow::setupUi(this)`
- **`src/ui/settingsdialog.ui`** — `SettingsDialog` calls `Ui::SettingsDialog::setupUi(this)`
  (categories/tabs are registered dynamically in code)

Open them in Qt Widget Designer:

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
