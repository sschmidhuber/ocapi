#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace ocapi {

class ChatSession;

// The chat history panel: a time-grouped tree of sessions. The toolbar
// actions (new chat, settings) live in the main window's toolbar.
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
    void sessionDeleteRequested(const QUuid &id);

private:
    QTreeWidget *m_tree;
};

} // namespace ocapi
