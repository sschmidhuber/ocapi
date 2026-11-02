#include "core/tooldebug.h"

#include "core/chatsession.h"

namespace ocapi::toolDebug {

void logSession(const ChatSession *session, const QString &label) {
    if (!enabled() || !session)
        return;
    log(label +
        QStringLiteral(": %1 message(s)").arg(static_cast<int>(session->messages().size())));
    for (const auto &message : session->messages()) {
        QString line = QStringLiteral("  [%1] id=%2 streaming=%3 contentLen=%4")
                           .arg(message.roleName(), message.id.toString(QUuid::WithoutBraces))
                           .arg(message.streaming)
                           .arg(static_cast<int>(message.content.size()));
        if (!message.reasoning.isEmpty())
            line +=
                QStringLiteral(" reasoningLen=%1").arg(static_cast<int>(message.reasoning.size()));
        if (message.hasToolCalls()) {
            QJsonArray calls;
            for (const auto &call : message.toolCalls) {
                calls.append(QJsonObject{
                    {QStringLiteral("id"), call.id},
                    {QStringLiteral("name"), call.name},
                    {QStringLiteral("arguments"), call.arguments},
                    {QStringLiteral("status"), static_cast<int>(call.status)},
                    {QStringLiteral("resultLen"), static_cast<int>(call.result.size())},
                });
            }
            line +=
                QStringLiteral(" toolCalls=%1")
                    .arg(QString::fromUtf8(QJsonDocument(calls).toJson(QJsonDocument::Compact)));
        }
        if (message.role == Role::Tool)
            line += QStringLiteral(" toolCallId=%1 toolName=%2 content=%3")
                        .arg(message.toolCallId, message.toolName, message.content.left(300));
        log(line);
    }
}

} // namespace ocapi::toolDebug
