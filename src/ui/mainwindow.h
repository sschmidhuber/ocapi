#pragma once

#include "core/backendregistry.h"
#include "core/imagemessage.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/message.h"
#include "core/prompttemplateregistry.h"
#include "core/shortcutregistry.h"
#include "core/sttregistry.h"
#include "core/systemprompt.h"
#include "core/ttsplayer.h"
#include "core/ttsregistry.h"
#include "core/uisettings.h"
#include "core/voiceinput.h"
#include "storage/chatdatabase.h"
#include <QDateTime>
#include <QPair>

#include <QHash>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
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

// One in-flight model turn per chat: chats run in parallel, so the backend
// instance and the streaming/tool-round bookkeeping live per session
// instead of as singletons in the main window.
struct StreamContext;

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
    [[nodiscard]] ShortcutRegistry &shortcutsForTest() { return m_shortcuts; }
    [[nodiscard]] PromptTemplateRegistry &promptTemplatesForTest() { return m_promptTemplates; }
    void requestDeleteSessionsForTest(const QList<QUuid> &ids) { onSessionDeleteRequested(ids); }
    void markUnreadForTest(const QUuid &id) { m_unreadSessions.insert(id); }
    // Drives the streaming UI state of the current chat directly.
    void simulateStreamStartForTest();
    void simulateStreamFinishForTest();
    void sendTextForTest(const QString &text) { onSend(text, {}, {}); }
    void startEditMessageForTest(const QUuid &id) {
        startEditMessage(id.toString(QUuid::WithoutBraces));
    }
    [[nodiscard]] bool isEditingMessageForTest() const { return !m_editingMessageId.isNull(); }
    // Simulates the backend requesting MCP tool calls on the current session.
    void simulateToolCallsForTest(const QVector<ToolCall> &calls) {
        if (!m_currentSession)
            return;
        onToolCallsRequested(ensureContextForTest(m_currentSession), QUuid::createUuid(), calls);
    }

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onNewChat();
    void onSessionSelected(const QUuid &id);
    // Editing a user message: loads it into the composer; Send truncates the
    // history from that message on and resends, Cancel aborts.
    void startEditMessage(const QString &idString);
    void cancelEditMessage();
    // Prompt-template autocompletion popup above the composer (non-modal,
    // the composer keeps focus and keeps filtering while typing).
    void showPromptTemplateMenu();
    void onPromptFilterChanged(const QString &filter);
    void confirmPromptMenu(int row = -1);
    void closePromptMenu();
    [[nodiscard]] QWidget *promptMenuForTest() const { return m_promptMenu; }
    // Deletes the given chats; asks for confirmation when there are several.
    // Running and unread chats are never deleted.
    void onSessionDeleteRequested(const QList<QUuid> &ids);
    // Selects the next (direction 1) or previous (direction -1) chat of the
    // history in the sidebar's order, wrapping around at both ends.
    void cycleChat(int direction);
    void openSettings(const QString &pageId = {});
    void onSend(const QString &text, const QVector<ImageAttachment> &images = {},
                const QVector<FileAttachment> &files = {});
    // Sends the next queued follow-up message if no turn is running.
    void sendPendingIfIdle();
    void onStop();
    void onBackendChanged();
    void onModelSelected(const QString &backendId, const QString &model);
    void onModelsDetected(const QString &backendId, const QStringList &models);
    // Adds detected models to the picker's model list for the backend.
    void addModelsToPicker(const QString &backendId, const QStringList &models);
    void onToolCallsRequested(StreamContext *context, const QUuid &messageId,
                              const QVector<ToolCall> &toolCalls);
    // Marks the matching tool call finished and persists its status/result.
    void onToolResultReady(const QString &toolName, const QString &resultText, bool isError);
    void onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                  const QString &toolName, const QJsonObject &arguments);
    void onMcpToolsChanged();
    void onSpeakResponsesToggled();

private:
    void setupUi();
    void setupConnections();
    // Creates the window-level QShortcut objects from the sequences in the
    // shortcut registry; reapplyShortcuts() re-creates them after changes.
    void createWindowShortcuts();
    // Re-creates the window shortcuts and the composer's dictate shortcut
    // from the current registry state and refreshes the tooltips.
    void reapplyShortcuts();
    // Chat-view signals: older-messages paging, link open/hover, file open.
    void setupChatViewConnections();
    // Wires the composer splitter (drag to resize, persist) and the
    // attachment-driven growth of the composer column.
    void setupComposerSplitter();
    // Sizes the composer column to the persisted base height plus the
    // attachment strip (chat area shrinks to make room for thumbnails).
    void applyComposerHeight();
    void onComposerSplitterMoved(int pos, int index);
    void connectContext(StreamContext *context);
    void onTokenReceived(StreamContext *context, const QUuid &messageId, const QString &text);
    void onReasoningReceived(StreamContext *context, const QUuid &messageId, const QString &text);
    void onStreamFinished(StreamContext *context, const QUuid &messageId);
    void onStreamFailed(StreamContext *context, const QUuid &messageId, const QString &error);
    void createSession(const QString &backendId = {}, const QString &model = {});
    void deleteSession(const QUuid &id);
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
    void loadSystemPrompt();
    void loadLastModels();
    void loadShortcuts();
    void loadUiSettings();
    void loadPromptTemplates();
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
    [[nodiscard]] static QString messageContent(StreamContext *context, const QUuid &messageId);
    [[nodiscard]] double throughputFor(StreamContext *context, const QUuid &messageId);
    // Ends the turn of one chat: clears its streaming state, updates the UI
    // and schedules the context (with its backend) for deletion.
    void finalizeContext(StreamContext *context, const QString &statusMessage);
    void detectModels();
    // Runs the MCP tool-call loop for the chat: executes the requested
    // tools and re-sends the conversation until the model answers with text.
    void runToolCalls(StreamContext *context, const QUuid &assistantMessageId,
                      const QVector<ToolCall> &toolCalls);
    void continueAfterToolResults(StreamContext *context);
    void sendConversationAfterToolResults(StreamContext *context);

    // Per-chat turn state (see StreamContext). A context exists while the
    // chat's model is streaming or running tools.
    [[nodiscard]] StreamContext *contextFor(const ChatSession *session) const;
    [[nodiscard]] StreamContext *ensureContext(ChatSession *session);
    [[nodiscard]] StreamContext *ensureContextForTest(ChatSession *session) {
        return ensureContext(session);
    }
    [[nodiscard]] bool contextAlive(const StreamContext *context) const;
    // Same check for callbacks (timers, session signals) that can outlive the
    // context: never dereferences it, so a deleted context is detected safely.
    [[nodiscard]] bool contextAliveFor(const QUuid &sessionId, const StreamContext *context) const;
    void destroyContext(StreamContext *context);
    // Cancels and drops the turn of one chat (stop button, deleted chat,
    // backend switch of that chat).
    void cancelContext(ChatSession *session);
    // Reflects the current chat's turn state in composer, stop button and
    // status bar, and pushes the running/unread badges to the sidebar.
    void updateStreamingUi();
    // Marks the first running tool call with the given name (across all
    // chats) done/failed; returns whether one was found.
    [[nodiscard]] bool markRunningToolCallResult(const QString &toolName, const QString &resultText,
                                                 bool isError);
    [[nodiscard]] bool markRunningToolCallResultIn(ChatSession *session, const QString &toolName,
                                                   const QString &resultText, bool isError);
    void markChatUnread(const ChatSession *session);
    [[nodiscard]] QSet<QUuid> runningSessionIds() const;

    BackendRegistry m_registry;
    SttRegistry m_sttRegistry;
    TtsRegistry m_ttsRegistry;
    SystemPrompt m_systemPrompt;
    TtsPlayer m_ttsPlayer;
    VoiceInput m_voiceInput;
    McpRegistry m_mcpRegistry;
    McpManager m_mcpManager;
    ChatDatabase m_database;
    QVector<ChatSession *> m_sessions;
    ChatSession *m_currentSession = nullptr;
    // One in-flight model turn per chat, keyed by session id: chats run in
    // parallel and switching chats never interrupts a running one.
    QHash<QUuid, StreamContext *> m_streams;
    // Chats that produced assistant output the user has not seen yet.
    QSet<QUuid> m_unreadSessions;
    // The context whose tool call is currently being executed; permission
    // requests are emitted synchronously from executeToolCall, so the
    // context is read at the start of onMcpPermissionRequested.
    StreamContext *m_toolContext = nullptr;
    // One in-flight model detection per backend id.
    QHash<QString, IBackend *> m_detectors;
    QSet<QString> m_pendingDetection;
    bool m_loadingOlder = false;
    // Models detected per backend id (survives session switches).
    QHash<QString, QStringList> m_detectedModels;
    // Last model selected per backend id, loaded from/saved to ocapi.conf.
    QHash<QString, QString> m_lastModels;
    QString m_dictatedText;
    // Messages typed while a turn was still running; sent in order once the
    // current turn finishes (the composer stays editable while streaming).
    struct PendingMessage {
        QString text;
        QVector<ImageAttachment> images;
        QVector<FileAttachment> files;
    };
    QVector<PendingMessage> m_pendingMessages;
    // Configurable keyboard shortcuts (loaded from ocapi.conf) and the
    // QShortcut objects currently bound to them.
    ShortcutRegistry m_shortcuts;
    QList<QShortcut *> m_windowShortcuts;
    // General UI settings (Settings > General > User Interface).
    UiSettings m_uiSettings;
    // Prompt templates (Settings > General > Prompt Templates).
    PromptTemplateRegistry m_promptTemplates;
    // Autocompletion popup (frameless top-level above the composer).
    QWidget *m_promptMenu = nullptr;
    QListWidget *m_promptMenuList = nullptr;

    Sidebar *m_sidebar = nullptr;
    WebChatView *m_chatView = nullptr;
    Composer *m_composer = nullptr;
    // Vertical splitter between chat area and composer column.
    QSplitter *m_rightSplitter = nullptr;
    QWidget *m_composerColumn = nullptr;
    QToolBar *m_toolBar = nullptr;
    QToolButton *m_newChatButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    ModelPickerButton *m_modelPicker = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_cancelEditButton = nullptr;
    // The user message currently being edited (null uuid when not editing).
    QUuid m_editingMessageId;
    QToolButton *m_speakButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_mcpStatusLabel = nullptr;
};

} // namespace ocapi
