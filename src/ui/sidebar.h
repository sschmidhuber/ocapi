#pragma once

#include <QSet>
#include <QUuid>
#include <QWidget>

class QTimer;
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

    // Per-session state badges: an animated spinner for chats whose model
    // is still working, a dot for chats with messages the user has not
    // seen yet. Applied on the next (or the current) tree build.
    void setIndicators(const QSet<QUuid> &running, const QSet<QUuid> &unread);

signals:
    void sessionSelected(const QUuid &id);
    void sessionDeleteRequested(const QUuid &id);

private:
    void applyIndicators();
    void advanceSpinner();
    [[nodiscard]] QIcon spinnerIcon() const;
    [[nodiscard]] QIcon unreadDotIcon() const;

    QTreeWidget *m_tree;
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerAngle = 0;
    QSet<QUuid> m_running;
    QSet<QUuid> m_unread;
};

} // namespace ocapi
