#pragma once

#include "core/backendregistry.h"
#include "core/message.h"
#include "storage/chatdatabase.h"

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
    void onManageBackends();
    void onSend(const QString &text);
    void onStop();
    void onBackendChanged();
    void onModelSelected(const QString &model);
    void onModelsDetected(const QStringList &models);

private:
    void createSession(const QString &backendId = {}, const QString &model = {});
    void refreshSidebar();
    void refreshBackendSelector();
    void updateEmptyState();
    [[nodiscard]] bool openDatabase();
    void loadBackendsFromDatabase();
    void loadOlderMessages(ChatSession *session);
    void persistSession(ChatSession *session);
    void persistMessageForSession(ChatSession *session, const Message &message);
    void syncSessionToDatabase(ChatSession *session);
    [[nodiscard]] QString messageContent(const QUuid &messageId) const;
    void endStreaming(const QString &statusMessage);
    IBackend *activeBackend();
    void connectBackend(IBackend *backend);
    void disconnectBackend();
    void detectModels();

    BackendRegistry m_registry;
    ChatDatabase m_database;
    QVector<ChatSession *> m_sessions;
    ChatSession *m_currentSession = nullptr;
    std::unique_ptr<IBackend> m_backend;
    std::unique_ptr<IBackend> m_detectorBackend;
    bool m_streaming = false;
    bool m_loadingOlder = false;
    QString m_detectedBackendId;

    Sidebar *m_sidebar = nullptr;
    ChatView *m_chatView = nullptr;
    Composer *m_composer = nullptr;
    QComboBox *m_backendCombo = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
};

} // namespace ocapi
