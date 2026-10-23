#pragma once

#include "core/message.h"
#include <QListView>
#include <QVector>

class QMouseEvent;

namespace ocapi {

class ChatSession;

class ChatView : public QListView {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);
    void setSession(ChatSession *session);
    void prependMessages(const QVector<Message> &messages);

signals:
    void olderMessagesRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void checkScrollTop();

private:
    void rebuild();
    void scrollToEnd();
    static void copyMessage(const QModelIndex &index);
    ChatSession *m_session = nullptr;
    QVector<Message> m_messages;
};

} // namespace ocapi
