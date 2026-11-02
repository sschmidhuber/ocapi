#include "core/chatsession.h"

#include "ui/webchatview.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineSettings>

namespace ocapi {

namespace {

QString jsString(const QString &s) {
    QJsonArray arr;
    arr.append(s);
    QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2); // strip the array brackets -> bare JSON string
}

QString toolCallStatusToJs(ToolCallStatus status) {
    switch (status) {
    case ToolCallStatus::Done:
        return QStringLiteral("done");
    case ToolCallStatus::Failed:
        return QStringLiteral("failed");
    case ToolCallStatus::Running:
        break;
    }
    return QStringLiteral("running");
}

// Tool calls of one message as a JSON array, passed to the page as a JS
// literal: [{id, name, arguments, status, result}, ...] in call order.
QString toolCallsToJs(const QVector<ToolCall> &toolCalls) {
    QJsonArray arr;
    for (const auto &call : toolCalls) {
        arr.append(QJsonObject{
            {QStringLiteral("id"), call.id},
            {QStringLiteral("name"), call.name},
            {QStringLiteral("arguments"), call.arguments},
            {QStringLiteral("status"), toolCallStatusToJs(call.status)},
            {QStringLiteral("result"), call.result},
        });
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString messageToJs(const Message &m) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = m.id.toString(QUuid::WithoutBraces);
    obj[QStringLiteral("role")] = m.roleName();
    obj[QStringLiteral("content")] = m.content;
    obj[QStringLiteral("streaming")] = m.streaming;
    obj[QStringLiteral("reasoning")] = m.reasoning;
    obj[QStringLiteral("reasoningStreaming")] = m.reasoningStreaming;
    obj[QStringLiteral("error")] = m.error;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

WebChatView::WebChatView(QWidget *parent) : QWebEngineView(parent) {
    m_bridge = new WebChatBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("qtBridge"), m_bridge);
    connect(m_bridge, &WebChatBridge::scrolledToTop, this, &WebChatView::olderMessagesRequested);
    page()->setWebChannel(m_channel);

    settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);

    connect(this, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok)
            return;
        m_ready = true;
        if (m_session) {
            for (const auto &message : m_session->messages())
                onMessageAdded(message);
        }
    });

    setUrl(QUrl(QStringLiteral("qrc:/resources/web/chat.html")));
}

void WebChatView::callJs(const QString &expr) {
    if (m_ready)
        page()->runJavaScript(expr);
}

void WebChatView::setSession(ChatSession *session) {
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    callJs(QStringLiteral("clearLog()"));
    if (!m_session)
        return;
    connect(m_session, &ChatSession::messageAdded, this, &WebChatView::onMessageAdded);
    connect(m_session, &ChatSession::messageUpdated, this, &WebChatView::onMessageUpdated);
    for (const auto &message : m_session->messages())
        onMessageAdded(message);
}

void WebChatView::prependMessages(const QVector<Message> &messages) {
    // Web view appends; older messages are prepended by re-setting the session
    // content. For simplicity, re-render all messages in order.
    if (!m_session)
        return;
    callJs(QStringLiteral("clearLog()"));
    for (const auto &message : m_session->messages())
        onMessageAdded(message);
}

void WebChatView::onMessageAdded(const Message &message) {
    callJs(QStringLiteral("setMessage(%1, %2, %3, %4, %5, %6, %7)")
               .arg(jsString(message.id.toString(QUuid::WithoutBraces)),
                    jsString(message.roleName()), jsString(message.content),
                    message.streaming ? QStringLiteral("true") : QStringLiteral("false"),
                    jsString(message.reasoning),
                    message.reasoningStreaming ? QStringLiteral("true") : QStringLiteral("false"),
                    toolCallsToJs(message.toolCalls)));
}

void WebChatView::onMessageUpdated(const Message &message) {
    onMessageAdded(message);
}

} // namespace ocapi
