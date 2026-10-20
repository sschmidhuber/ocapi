#pragma once

#include "core/imagemessage.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

// One tool invocation requested by the assistant (OpenAI-style tool_calls).
struct ToolCall {
    QString id;
    QString name; // namespaced tool name, e.g. "server__tool"
    QJsonObject arguments;

    [[nodiscard]] bool operator==(const ToolCall &other) const {
        return id == other.id && name == other.name && arguments == other.arguments;
    }
};

} // namespace ocapi

Q_DECLARE_METATYPE(ocapi::ToolCall)
Q_DECLARE_METATYPE(QVector<ocapi::ToolCall>)

namespace ocapi {

struct Message {
    QUuid id = QUuid::createUuid();
    Role role = Role::User;
    QString content;
    QDateTime timestamp = QDateTime::currentDateTime();
    bool streaming = false;
    QString error;
    double tokensPerSecond = -1.0;   // output tokens/s; negative when unknown
    QVector<ImageAttachment> images; // images attached to this message
    QVector<ToolCall> toolCalls;     // tool invocations requested by the assistant
    // For Role::Tool: which tool call this message answers.
    QString toolCallId;
    QString toolName;

    [[nodiscard]] bool hasImages() const { return !images.isEmpty(); }
    [[nodiscard]] bool hasToolCalls() const { return !toolCalls.isEmpty(); }
    [[nodiscard]] bool isEmpty() const {
        return content.isEmpty() && images.isEmpty() && toolCalls.isEmpty();
    }

    [[nodiscard]] QString roleName() const {
        switch (role) {
        case Role::System:
            return QStringLiteral("system");
        case Role::User:
            return QStringLiteral("user");
        case Role::Assistant:
            return QStringLiteral("assistant");
        case Role::Tool:
            return QStringLiteral("tool");
        }
        return QStringLiteral("unknown");
    }
};

} // namespace ocapi
