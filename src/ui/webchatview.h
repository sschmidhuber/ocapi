#pragma once

#include "core/message.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QList>
#include <QUrl>
#include <QVector>
#include <QWebEngineView>

class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QImage;
class QWebChannel;

namespace ocapi {
class ChatSession;

// Bridge object exposed to the chat page's JavaScript: the page reports
// scroll-to-top ("load older" detection), asks for clipboard copies of
// message content (QWebEngine cannot write the clipboard from qrc pages),
// requests opening links in the system browser and reports hovered link
// URLs for the host's status bar.
class WebChatBridge : public QObject {
    Q_OBJECT
public:
    explicit WebChatBridge(QObject *parent = nullptr) : QObject(parent) {}
signals:
    void scrolledToTop();
    void linkActivated(const QString &url);
    void linkHovered(const QString &url); // empty when the pointer leaves the link
    void attachmentOpenRequested(const QString &path);
    void messageEditRequested(const QString &id);
public slots:
    void notifyScrolledToTop() { emit scrolledToTop(); }
    void openUrl(const QString &url) { emit linkActivated(url); }
    void hoverLink(const QString &url) { emit linkHovered(url); }
    void openAttachment(const QString &path) { emit attachmentOpenRequested(path); }
    void editMessage(const QString &id) { emit messageEditRequested(id); }
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
    // Whether assistant messages show their output token throughput (t/s)
    // next to the copy button; re-renders the current session on change.
    void setShowThroughput(bool enabled);

signals:
    void olderMessagesRequested();
    // A link in a chat message was clicked / hovered (empty url = left).
    void linkActivated(const QString &url);
    void linkHovered(const QString &url);
    // A file element in a chat message was clicked (open in default app).
    void attachmentOpenRequested(const QString &path);
    // The edit button of a user message was clicked.
    void messageEditRequested(const QString &id);
    // Files/images dragged onto the chat view (forwarded to the composer).
    void urlsDropped(const QList<QUrl> &urls);
    void imageDropped(const QImage &image);

private:
    void callJs(const QString &expr);
    void onMessageAdded(const Message &message);
    void onMessageUpdated(const Message &message);
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void savePageAs();
    void showPageSource();

    ChatSession *m_session = nullptr;
    QWebChannel *m_channel = nullptr;
    WebChatBridge *m_bridge = nullptr;
    bool m_ready = false;
    bool m_showThroughput = false;
};

} // namespace ocapi
