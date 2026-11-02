#pragma once

#include "core/backendregistry.h"
#include "core/imagemessage.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/message.h"
#include "core/sttregistry.h"
#include "core/ttsplayer.h"
#include "core/ttsregistry.h"
#include "core/voiceinput.h"
#include "storage/chatdatabase.h"
#include <QDateTime>
#include <QPair>

#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QToolBar>
#include <QToolButton>
#include <QVector>
#include <memory>

namespace ocapi {

class ChatSession;
class WebChatView;
class Composer;
class IBackend;
class Sidebar;

// Compact single-widget model picker: one button showing "Backend | model"
// with a menu that lists each backend's models as submenus.
class ModelPickerButton : public QToolButton {
    Q_OBJECT

public:
    explicit ModelPickerButton(QWidget *parent = nullptr);

    void setBackendAndModel(const QString &backendName, const QString &backendId,
                            const QString &model);
    [[nodiscard]] QString currentBackendId() const { return m_backendId; }
    [[nodiscard]] QString currentModel() const { return m_model; }

    // Replaces the menu with one section per backend; each section lists the
    // models known for that backend.
    void setBackends(const QVector<QPair<QString, QString>> &backends,
                     const QHash<QString, QStringList> &models);

    // Adds models for one backend (e.g. detected via listModels) and rebuilds
    // the menu, keeping the current selection.
    void addModels(const QString &backendId, const QStringList &models);

    [[nodiscard]] const QVector<QPair<QString, QString>> &backends() const { return m_backends; }
    [[nodiscard]] const QHash<QString, QStringList> &backendModels() const { return m_models; }
    [[nodiscard]] QString backendName() const { return m_backendName; }

signals:
    void backendSelected(const QString &backendId);
    void modelSelected(const QString &backendId, const QString &model);

private:
    void rebuildMenu();
    QString m_backendName;
    QString m_backendId;
    QString m_model;
    QVector<QPair<QString, QString>> m_backends;
    QHash<QString, QStringList> m_models;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Test access to the current session, the new-chat path and the backend
    // registry (to seed backends in tests).
    [[nodiscard]] ChatSession *currentSession() const { return m_currentSession; }
    void startNewChat() { onNewChat(); }
    [[nodiscard]] BackendRegistry &registryForTest() { return m_registry; }
    // Simulates the backend requesting MCP tool calls on the current session.
    void simulateToolCallsForTest(const QVector<ToolCall> &calls) {
        if (m_currentSession)
            onToolCallsRequested(QUuid::createUuid(), calls);
    }

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onNewChat();
    void onSessionSelected(const QUuid &id);
    void onSessionDeleteRequested(const QUuid &id);
    void openSettings(const QString &pageId = {});
    void onSend(const QString &text, const QVector<ImageAttachment> &images = {},
                const QVector<FileAttachment> &files = {});
    void onStop();
    void onBackendChanged();
    void onModelSelected(const QString &backendId, const QString &model);
    void onModelsDetected(const QString &backendId, const QStringList &models);
    // Adds detected models to the picker's model list for the backend.
    void addModelsToPicker(const QString &backendId, const QStringList &models);
    void onToolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls);
    // Marks the matching tool call finished and persists its status/result.
    void onToolResultReady(const QString &toolName, const QString &resultText, bool isError);
    void onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                  const QString &toolName, const QJsonObject &arguments);
    void onMcpToolsChanged();
    void onSpeakResponsesToggled();

private:
    void setupUi();
    void setupConnections();
    void connectStreamingSignals(IBackend *backend) const;
    void onTokenReceived(const QUuid &messageId, const QString &text);
    void onReasoningReceived(const QUuid &messageId, const QString &text);
    void onStreamFinished(const QUuid &messageId);
    void onStreamFailed(const QUuid &messageId, const QString &error);
    void createSession(const QString &backendId = {}, const QString &model = {});
    void refreshSidebar();
    void refreshBackendSelector();
    // Previously detected models for one backend (plus the persisted
    // last-selected model, which is kept even when the backend is offline).
    [[nodiscard]] QStringList knownModelsForBackend(const BackendConfig &config) const;
    // Last model the user picked for a backend; persisted in ocapi.conf so
    // the selection survives restarts. Empty when never selected.
    [[nodiscard]] QString lastModelForBackend(const QString &backendId) const;
    void saveLastModelForBackend(const QString &backendId, const QString &model);
    void forgetLastModelForBackend(const QString &backendId);
    void updateEmptyState();
    [[nodiscard]] bool openDatabase();
    void loadBackendsFromDatabase();
    void loadSttSettingsFromDatabase();
    void loadLastModels();
    void configureVoiceInput();
    void loadTtsSettingsFromDatabase();
    void configureTts();
    void loadMcpServersFromDatabase();
    void loadOlderMessages(ChatSession *session);
    void persistSession(ChatSession *session);
    void persistMessageForSession(ChatSession *session, const Message &message);
    // Re-persists an assistant message so its current tool calls (status and
    // results) are stored.
    void persistToolCalls(ChatSession *session, const QUuid &messageId);
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
    void sendConversationAfterToolResults(ChatSession *session);

    BackendRegistry m_registry;
    SttRegistry m_sttRegistry;
    TtsRegistry m_ttsRegistry;
    TtsPlayer m_ttsPlayer;
    VoiceInput m_voiceInput;
    McpRegistry m_mcpRegistry;
    McpManager m_mcpManager;
    ChatDatabase m_database;
    QVector<ChatSession *> m_sessions;
    ChatSession *m_currentSession = nullptr;
    std::unique_ptr<IBackend> m_backend;
    // One in-flight model detection per backend id.
    QHash<QString, IBackend *> m_detectors;
    QSet<QString> m_pendingDetection;
    bool m_streaming = false;
    bool m_loadingOlder = false;
    bool m_runningTools = false; // a tool-call round is in progress
    QUuid m_streamedMessageId;
    // Assistant message of the current turn. Tool-call rounds continue this
    // same message instead of appending a new one, so Thinking, tool calls
    // and the answer text all live in a single chat message.
    QUuid m_turnAssistantMessageId;
    int m_streamedTokens = 0;
    QDateTime m_firstToken;
    // Models detected per backend id (survives session switches).
    QHash<QString, QStringList> m_detectedModels;
    // Last model selected per backend id, loaded from/saved to ocapi.conf.
    QHash<QString, QString> m_lastModels;
    QString m_dictatedText;
    // Session waiting for tool results before the next model round.
    ChatSession *m_toolSession = nullptr;
    int m_pendingToolRounds = 0;

    Sidebar *m_sidebar = nullptr;
    WebChatView *m_chatView = nullptr;
    Composer *m_composer = nullptr;
    QToolBar *m_toolBar = nullptr;
    QToolButton *m_newChatButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    ModelPickerButton *m_modelPicker = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QToolButton *m_speakButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_mcpStatusLabel = nullptr;
};

} // namespace ocapi
