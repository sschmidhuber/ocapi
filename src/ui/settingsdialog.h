#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

namespace ocapi {

class BackendRegistry;
class McpManager;
class McpRegistry;
class SttRegistry;

// Unified settings dialog that hosts the model backends, MCP server and
// speech-to-text pages, so configuration lives in a separate window instead
// of the chat window.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                   McpManager *mcpManager, SttRegistry *sttRegistry, QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void selectPage(const QString &pageId);

private:
    void addPage(const QString &title, const QString &pageId, QWidget *page);

    QListWidget *m_pages = nullptr;
    QStackedWidget *m_stack = nullptr;
};

} // namespace ocapi
