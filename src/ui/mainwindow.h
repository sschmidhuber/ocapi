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
#include <QToolButton>
#include <QVector>
#include <memory>

namespace ocapi {

class ChatSession;
class ChatView;
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
    void onModelsDetected(const QStringList &models);
    // Adds detected models to the picker's model list for the backend.
    void addModelsToPicker(const QString &backendId, const QStringList &models);
    void onToolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls);
    void onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                  const QString &toolName, const QJsonObject &arguments);
    void onMcpToolsChanged();
    void onSpeakResponsesToggled();

private:
    void setupUi();
    void setupConnections();
    void connectStreamingSignals(IBackend *backend) const;
    void onTokenReceived(const QUuid &messageId, const QString &text);
    void onStreamFinished(const QUuid &messageId);
    void onStreamFailed(const QUuid &messageId, const QString &error);
    void createSession(const QString &backendId = {}, const QString &model = {});
    void refreshSidebar();
    void refreshBackendSelector();
    // Default model plus previously detected models for one backend.
    [[nodiscard]] QStringList knownModelsForBackend(const BackendConfig &config) const;
    void updateEmptyState();
    [[nodiscard]] bool openDatabase();
    void loadBackendsFromDatabase();
    void loadSttSettingsFromDatabase();
    void configureVoiceInput();
    void loadTtsSettingsFromDatabase();
    void configureTts();
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
    TtsRegistry m_ttsRegistry;
    TtsPlayer m_ttsPlayer;
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
    // Models detected per backend id (survives session switches).
    QHash<QString, QStringList> m_detectedModels;
    QString m_dictatedText;
    // Session waiting for tool results before the next model round.
    ChatSession *m_toolSession = nullptr;
    int m_pendingToolRounds = 0;

    Sidebar *m_sidebar = nullptr;
    ChatView *m_chatView = nullptr;
    Composer *m_composer = nullptr;
    ModelPickerButton *m_modelPicker = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_speakButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_mcpStatusLabel = nullptr;
};

} // namespace ocapi
