#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;
class QStackedWidget;
class QTabWidget;

namespace ocapi {

class BackendRegistry;
class McpManager;
class McpRegistry;
class SttRegistry;
class TtsRegistry;

// Settings dialog following the structure of KDE's KCM configuration
// dialogs: the top-level categories (for example "Models" or "Tools")
// are listed in a sidebar on the left, and each category shows its pages
// (for example "LLM", "STT", "TTS", "MCP") as tabs on the right.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                   McpManager *mcpManager, SttRegistry *sttRegistry, TtsRegistry *ttsRegistry,
                   QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // Selects the category ("models", "tools") and, if the category has
    // tabs, the tab with the given id ("llm", "stt", "tts", "mcp").
    void selectPage(const QString &pageId);

private:
    // Adds a top-level category to the sidebar; its pages can then be
    // registered with addTab(). The category shows the icon looked up under
    // the given theme name, with the bundled SVG as fallback.
    void addCategory(const QString &title, const QString &categoryId, const QString &iconThemeName,
                     const QString &iconPath);

    // Adds a page as a new tab of the most recently added category. The
    // dialog takes ownership of the page.
    void addTab(const QString &title, const QString &tabId, QWidget *page);

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<QTabWidget *> m_tabWidgets;
    QList<int> m_tabOffsets;
    QStringList m_tabIds;
};

} // namespace ocapi
