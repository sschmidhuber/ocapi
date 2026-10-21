#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

namespace ocapi {

class BackendRegistry;
class McpManager;
class McpRegistry;
class SttRegistry;
class TtsRegistry;

// Unified settings dialog that hosts the model backends, MCP server,
// speech-to-text and text-to-speech pages, so configuration lives in a
// separate window instead of the chat window.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(BackendRegistry *backendRegistry, McpRegistry *mcpRegistry,
                   McpManager *mcpManager, SttRegistry *sttRegistry, TtsRegistry *ttsRegistry,
                   QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void selectPage(const QString &pageId);

private:
    void addPage(const QString &title, const QString &pageId, QWidget *page);

    QListWidget *m_pages = nullptr;
    QStackedWidget *m_stack = nullptr;
};

} // namespace ocapi
