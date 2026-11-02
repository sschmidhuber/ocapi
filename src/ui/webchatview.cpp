#include "core/chatsession.h"

#include "ui/webchatview.h"

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

QString messageToJs(const Message &m) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = m.id.toString(QUuid::WithoutBraces);
    obj[QStringLiteral("role")] = m.roleName();
    obj[QStringLiteral("content")] = m.content;
    obj[QStringLiteral("streaming")] = m.streaming;
    obj[QStringLiteral("reasoning")] = m.reasoning;
    obj[QStringLiteral("reasoningStreaming")] = m.reasoningStreaming;
}

void WebChatView::onMessageUpdated(const Message &message) {
    onMessageAdded(message);
}

} // namespace ocapi
