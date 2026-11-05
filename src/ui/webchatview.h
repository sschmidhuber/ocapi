#pragma once

#include "core/message.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QVector>
#include <QWebEngineView>

class QContextMenuEvent;
class QWebChannel;

namespace ocapi {
class ChatSession;

// Bridge object exposed to the chat page's JavaScript: the page reports
// scroll-to-top ("load older" detection) and asks for clipboard copies of
// message content (QWebEngine cannot write the clipboard from qrc pages).
class WebChatBridge : public QObject {
    Q_OBJECT
public:
    explicit WebChatBridge(QObject *parent = nullptr) : QObject(parent) {}
signals:
    void scrolledToTop();
public slots:
    void notifyScrolledToTop() { emit scrolledToTop(); }
    static void copyToClipboard(const QString &text) {
        QGuiApplication::clipboard()->setText(text);
    }
};

// Conversation area rendered by a QWebEngineView: full markdown via marked.js,
// KaTeX for \( \) / \[ \] / $ $ math, highlight.js for code. Replaces ChatView.
class WebChatView : public QWebEngineView {
    Q_OBJECT
public:
    explicit WebChatView(QWidget *parent = nullptr);

    void setSession(ChatSession *session);
    void prependMessages(const QVector<Message> &messages);

signals:
    void olderMessagesRequested();

private:
    void callJs(const QString &expr);
    void onMessageAdded(const Message &message);
    void onMessageUpdated(const Message &message);
    void contextMenuEvent(QContextMenuEvent *event) override;
    void savePageAs();
    void showPageSource();

    ChatSession *m_session = nullptr;
    QWebChannel *m_channel = nullptr;
    WebChatBridge *m_bridge = nullptr;
    bool m_ready = false;
};

} // namespace ocapi
