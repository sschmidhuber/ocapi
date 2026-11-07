#pragma once

#include <QList>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace ocapi {

class ChatSession;

// The chat history panel: a time-grouped tree of sessions. The toolbar
// actions (new chat, settings) live in the main window's toolbar. Chats can
// be multi-selected (Ctrl+click adds to the selection, Shift+click selects a
// range, selecting a group header includes all its chats); the selected
// chats can be deleted via the context menu's "Delete chat" or the Delete
// key. Chats that are running or unread are never deleted.
class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    // Rebuilds the tree, grouping sessions by their last activity time
    // (Today, Yesterday, weekday, date, month, year).
    void setSessions(const QVector<ChatSession *> &sessions);
    void selectSession(const QUuid &id);
    [[nodiscard]] QUuid currentSessionId() const;

    // Session ids of the current selection; a selected group header
    // contributes all of its chats.
    [[nodiscard]] QList<QUuid> selectedSessionIds() const;
    // The selected session ids that may be deleted (neither running nor
    // unread).
    [[nodiscard]] QList<QUuid> deletableSelectedSessionIds() const;

    // Per-session state badges: an animated spinner for chats whose model
    // is still working, a dot for chats with messages the user has not
    // seen yet. Applied on the next (or the current) tree build.
    void setIndicators(const QSet<QUuid> &running, const QSet<QUuid> &unread);

signals:
    void sessionSelected(const QUuid &id);
    // Emitted with the deletable selected session ids (context menu or Delete
    // key); the main window asks for confirmation when there are several and
    // performs the deletion.
    void sessionDeleteRequested(const QList<QUuid> &ids);

private:
    void applyIndicators();
    void advanceSpinner();
    void requestDeleteSelected();
    [[nodiscard]] QIcon spinnerIcon() const;
    [[nodiscard]] QIcon unreadDotIcon() const;

    QTreeWidget *m_tree;
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerAngle = 0;
    QSet<QUuid> m_running;
    QSet<QUuid> m_unread;
};

} // namespace ocapi
