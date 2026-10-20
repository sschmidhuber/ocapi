#pragma once

#include "core/message.h"

#include <QListView>
#include <QVector>

namespace ocapi {

class ChatSession;

class ChatView : public QListView {
    Q_OBJECT

public:
    explicit ChatView(QWidget *parent = nullptr);

    void setSession(ChatSession *session);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void scrollToEnd();

    ChatSession *m_session = nullptr;
    QVector<Message> m_messages;
};

} // namespace ocapi
