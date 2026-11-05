#include "core/chatsession.h"

#include "ui/webchatview.h"

#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPlainTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineContextMenuRequest>
#include <QWebEnginePage>
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

// Each thinking phase as a JSON array element, passed to the page as a JS
// literal: ["phase one", "phase two", ...].
QString reasoningToJs(const Message &m) {
    QJsonArray arr;
    for (const auto &segment : m.reasoningSegments)
        arr.append(segment);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// The message's element order as a JSON array, passed to the page as a JS
// literal: ["thinking:0", "tool:0", "thinking:1", ...].
QString elementOrderToJs(const Message &m) {
    QJsonArray arr;
    for (const auto &token : m.elementOrder)
        arr.append(token);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// Env-gated tracing of the chat element rendering: set OCAPI_DEBUG_CHAT to
// see on stderr, for every message pushed to the web view, the element
// sequence (thinking phases vs. tool calls) the page is told to render.
void logChatElements(const Message &m) {
    static const bool on = qEnvironmentVariableIsSet("OCAPI_DEBUG_CHAT");
    if (!on)
        return;
    QJsonArray calls;
    for (const auto &call : m.toolCalls) {
        calls.append(QJsonObject{
            {QStringLiteral("id"), call.id},
            {QStringLiteral("name"), call.name},
            {QStringLiteral("status"), static_cast<int>(call.status)},
        });
    }
    QJsonArray order;
    for (const auto &token : m.elementOrder)
        order.append(token);
    qWarning().noquote() << "[ocapi chat]" << m.id.toString(QUuid::WithoutBraces) << m.roleName()
                         << "streaming=" << m.streaming
                         << "thinkingPhases=" << m.reasoningSegments.size() << "toolCalls="
                         << QString::fromUtf8(QJsonDocument(calls).toJson(QJsonDocument::Compact))
                         << "elementOrder="
                         << QString::fromUtf8(QJsonDocument(order).toJson(QJsonDocument::Compact));
}

} // namespace

WebChatView::WebChatView(QWidget *parent) : QWebEngineView(parent) {
    m_bridge = new WebChatBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("qtBridge"), m_bridge);
    connect(m_bridge, &WebChatBridge::scrolledToTop, this, &WebChatView::olderMessagesRequested);
    page()->setWebChannel(m_channel);
    // The default page menu offers "Save Page"/"View Page Source", which do
    // nothing for a qrc:// page; replace it with working variants.
    connect(page(), &QWebEnginePage::contextMenuRequested, this,
            &WebChatView::onContextMenuRequested);

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
    logChatElements(message);
    callJs(QStringLiteral("setMessage(%1, %2, %3, %4, %5, %6, %7, %8)")
               .arg(jsString(message.id.toString(QUuid::WithoutBraces)),
                    jsString(message.roleName()), jsString(message.content),
                    message.streaming ? QStringLiteral("true") : QStringLiteral("false"),
                    reasoningToJs(message),
                    message.reasoningStreaming ? QStringLiteral("true") : QStringLiteral("false"),
                    toolCallsToJs(message.toolCalls), elementOrderToJs(message)));
}

void WebChatView::onMessageUpdated(const Message &message) {
    onMessageAdded(message);
}

void WebChatView::onContextMenuRequested(const QWebEngineContextMenuRequest *request) {
    QMenu menu(this);
    QAction *savePage = menu.addAction(tr("Save Page\u2026"));
    connect(savePage, &QAction::triggered, this, &WebChatView::savePageAs);
    QAction *viewSource = menu.addAction(tr("View Page Source"));
    connect(viewSource, &QAction::triggered, this, &WebChatView::showPageSource);
    menu.exec(request->globalPos());
}

void WebChatView::savePageAs() {
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save Page"), QStringLiteral("chat.html"),
                                     tr("HTML files (*.html);;All files (*)"));
    if (path.isEmpty())
        return;
    page()->toHtml([path](const QString &html) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(html.toUtf8());
    });
}

void WebChatView::showPageSource() {
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Page Source"));
    dialog->resize(800, 600);
    auto *edit = new QPlainTextEdit(dialog);
    edit->setReadOnly(true);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit);
    page()->toHtml([edit](const QString &html) { edit->setPlainText(html); });
    dialog->show();
}

} // namespace ocapi
