#pragma once

#include <QDialog>
#include <QStringList>
#include <memory>

class QListWidget;

namespace Ui {
class SettingsDialog;
}
class QStackedWidget;
class QTabWidget;

namespace okapi {

class BackendRegistry;
class McpManager;
class McpRegistry;
class PromptTemplateRegistry;
class ShortcutRegistry;
class SttRegistry;
class SystemPrompt;
class TtsRegistry;
class UiSettings;

// Settings dialog following the structure of KDE's KCM configuration
// dialogs: the top-level categories (for example "Models", "Tools" or
// "Shortcuts") are listed in a sidebar on the left, and each category shows
// its pages (for example "LLM", "STT", "TTS", "MCP") as tabs on the right.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                   McpManager *mcpManager, SttRegistry *sttRegistry, TtsRegistry *ttsRegistry,
                   SystemPrompt *systemPrompt, ShortcutRegistry *shortcutRegistry,
                   UiSettings *uiSettings, PromptTemplateRegistry *promptTemplates,
                   QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // Selects the category ("models", "tools") and, if the category has
    // tabs, the tab with the given id ("llm", "stt", "tts", "mcp").
    void selectPage(const QString &pageId);

private:
    // Adds a top-level category to the sidebar; its pages can then be
    // registered with addTab(). The category shows the icon looked up under
    // the given theme name, with the bundled SVG as fallback.
    void addCategory(const QString &title, const QString &categoryId, const QString &iconPath);

    // Adds a page as a new tab of the most recently added category. The
    // dialog takes ownership of the page.
    void addTab(const QString &title, const QString &tabId, QWidget *page);

    // The dialog shell (splitter, category list, stacked pages, button box)
    // is loaded from ui/settingsdialog.ui (AUTOUIC).
    std::unique_ptr<Ui::SettingsDialog> m_ui;

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<QTabWidget *> m_tabWidgets;
    QList<int> m_tabOffsets;
    QStringList m_tabIds;
};

} // namespace okapi
