#pragma once

#include "core/backendregistry.h"

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QHash>
#include <QVector>
#include <memory>

namespace ocapi {

class ChatSession;
class ChatView;
class Composer;
class IBackend;
class Sidebar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNewChat();
    void onSessionSelected(const QUuid& id);
    void onSessionDeleteRequested(const QUuid& id);
    void onManageBackends();
    void onSend(const QString& text);
    void onStop();
    void onBackendChanged();
    void onModelSelected(const QString& model);

private:
    void createSession(const QString& backendId = {}, const QString& model = {});
    void refreshSidebar();
    void refreshBackendSelector();
    void updateEmptyState();
    IBackend* activeBackend();
    void connectBackend(IBackend* backend);
    void disconnectBackend();

    BackendRegistry m_registry;
    QVector<ChatSession*> m_sessions;
    ChatSession* m_currentSession = nullptr;
    std::unique_ptr<IBackend> m_backend;
    bool m_streaming = false;

    Sidebar* m_sidebar = nullptr;
    ChatView* m_chatView = nullptr;
    Composer* m_composer = nullptr;
    QComboBox* m_backendCombo = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
};

} // namespace ocapi
