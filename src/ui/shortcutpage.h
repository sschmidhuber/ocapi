#pragma once

#include <QWidget>
#include <memory>

namespace Ui {
class ShortcutPage;
}

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace okapi {

class ShortcutRegistry;

// Settings page listing every keyboard shortcut of the app in a table
// (columns "Action" and "Shortcut", filterable via a search field, cf. the
// KDE shortcut configuration dialogs). Double-clicking a row (or the
// "Edit Shortcut..." button) opens a capture dialog for a new key sequence;
// "Reset to Default" restores the factory sequence. Changes are applied
// immediately and persisted to the application settings.
class ShortcutPage : public QWidget {
    Q_OBJECT

public:
    ~ShortcutPage() override;

    explicit ShortcutPage(ShortcutRegistry *registry, QWidget *parent = nullptr);

private slots:
    void onSearchChanged(const QString &text);
    void onEditShortcut();
    void onCellDoubleClicked(int row, int column);
    void onResetDefault();
    void onRegistryChanged();

private:
    std::unique_ptr<Ui::ShortcutPage> m_ui;
    void rebuildTable();
    void applyFilter();
    // Opens the capture dialog for the action of the given row and applies a
    // valid, conflict-free result to the registry. Returns whether the
    // registry was changed.
    bool captureAndApply(int row);
    void persist();

    ShortcutRegistry *m_registry;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_resetButton = nullptr;
};

} // namespace okapi
