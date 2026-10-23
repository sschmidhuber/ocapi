#pragma once

#include <QWidget>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace ocapi {

class ChatSession;

class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    // Rebuilds the tree, grouping sessions by their last activity time
    // (Today, Yesterday, weekday, date, month, year).
    void setSessions(const QVector<ChatSession *> &sessions);
    void selectSession(const QUuid &id);
    [[nodiscard]] QUuid currentSessionId() const;

signals:
    void sessionSelected(const QUuid &id);
    void newChatRequested();
    void settingsRequested(const QString &pageId);
    void sessionDeleteRequested(const QUuid &id);

private:
    QTreeWidget *m_tree;
    QPushButton *m_newChatButton;
    QPushButton *m_settingsButton;
};

} // namespace ocapi
