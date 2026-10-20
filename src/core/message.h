#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace ocapi {

enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

struct Message {
    QUuid id = QUuid::createUuid();
    Role role = Role::User;
    QString content;
    QDateTime timestamp = QDateTime::currentDateTime();
    bool streaming = false;
    QString error;
    double tokensPerSecond = -1.0; // output tokens/s; negative when unknown

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
