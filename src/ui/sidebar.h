#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace ocapi {

class ChatSession;

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    void setSessions(const QVector<ChatSession *> &sessions);
    void selectSession(const QUuid &id);
    [[nodiscard]] QUuid currentSessionId() const;

signals:
    void sessionSelected(const QUuid &id);
    void newChatRequested();
    void manageBackendsRequested();
    void manageMcpServersRequested();
    void sessionDeleteRequested(const QUuid &id);

private:
    QListWidget *m_list;
    QPushButton *m_newChatButton;
    QPushButton *m_backendsButton;
    QPushButton *m_mcpButton;
};

} // namespace ocapi
