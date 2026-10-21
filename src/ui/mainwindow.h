#pragma once

#include "core/backendregistry.h"
#include "core/imagemessage.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/message.h"
#include "core/sttregistry.h"
#include "core/voiceinput.h"
#include "storage/chatdatabase.h"
#include <QDateTime>

#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVector>
#include <memory>

namespace ocapi {

class ChatSession;
class ChatView;
class Composer;
class IBackend;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNewChat();
    void onSessionSelected(const QUuid &id);
    void onSessionDeleteRequested(const QUuid &id);
    void openSettings(const QString &pageId = {});
    void onSend(const QString &text, const QVector<ImageAttachment> &images = {});
    void onStop();
    void onBackendChanged();
    void onModelSelected(const QString &model);
    void onModelsDetected(const QStringList &models);
    void onToolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls);
    void onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                  const QString &toolName, const QJsonObject &arguments);
    void onMcpToolsChanged();

private:
    void createSession(const QString &backendId = {}, const QString &model = {});
    void refreshSidebar();
    void refreshBackendSelector();
    void updateEmptyState();
    [[nodiscard]] bool openDatabase();
    void loadBackendsFromDatabase();
    void loadSttSettingsFromDatabase();
    void configureVoiceInput();
    void loadMcpServersFromDatabase();
    void loadOlderMessages(ChatSession *session);
    void persistSession(ChatSession *session);
    void persistMessageForSession(ChatSession *session, const Message &message);
    void syncSessionToDatabase(ChatSession *session);
    [[nodiscard]] QString messageContent(const QUuid &messageId) const;
    [[nodiscard]] double throughputFor(const QUuid &messageId);
    void endStreaming(const QString &statusMessage);
    IBackend *activeBackend();
    void connectBackend(IBackend *backend);
    void disconnectBackend();
    void detectModels();
    // Runs the MCP tool-call loop for the session: executes the requested
    // tools and re-sends the conversation until the model answers with text.
    void runToolCalls(ChatSession *session, const QUuid &assistantMessageId,
                      const QVector<ToolCall> &toolCalls);
    void continueAfterToolResults(ChatSession *session);

    BackendRegistry m_registry;
    SttRegistry m_sttRegistry;
    VoiceInput m_voiceInput;
    McpRegistry m_mcpRegistry;
    McpManager m_mcpManager;
    ChatDatabase m_database;
    QVector<ChatSession *> m_sessions;
    ChatSession *m_currentSession = nullptr;
    std::unique_ptr<IBackend> m_backend;
    std::unique_ptr<IBackend> m_detectorBackend;
    bool m_streaming = false;
    bool m_loadingOlder = false;
    bool m_runningTools = false; // a tool-call round is in progress
    QUuid m_streamedMessageId;
    int m_streamedTokens = 0;
    QDateTime m_firstToken;
    QString m_detectedBackendId;
    QString m_dictatedText;
    // Session waiting for tool results before the next model round.
    ChatSession *m_toolSession = nullptr;
    int m_pendingToolRounds = 0;

    Sidebar *m_sidebar = nullptr;
    ChatView *m_chatView = nullptr;
    Composer *m_composer = nullptr;
    QComboBox *m_backendCombo = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_mcpStatusLabel = nullptr;
};

} // namespace ocapi
