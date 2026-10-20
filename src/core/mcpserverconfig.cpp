#include "core/mcpserverconfig.h"

namespace ocapi {

QString McpServerConfig::transportName(McpTransport transport) {
    switch (transport) {
    case McpTransport::Stdio:
        return QStringLiteral("stdio");
    case McpTransport::StreamableHttp:
        return QStringLiteral("Streamable HTTP");
    case McpTransport::HttpSse:
        return QStringLiteral("HTTP + SSE (legacy)");
    }
    return QStringLiteral("unknown");
}

QString McpServerConfig::transportKey(McpTransport transport) {
    switch (transport) {
    case McpTransport::Stdio:
        return QStringLiteral("stdio");
    case McpTransport::StreamableHttp:
        return QStringLiteral("http");
    case McpTransport::HttpSse:
        return QStringLiteral("sse");
    }
    return QStringLiteral("unknown");
}

McpTransport McpServerConfig::transportFromString(const QString &key) {
    if (key == QLatin1String("stdio"))
        return McpTransport::Stdio;
    if (key == QLatin1String("http"))
        return McpTransport::StreamableHttp;
    if (key == QLatin1String("sse"))
        return McpTransport::HttpSse;
    return McpTransport::StreamableHttp;
}

const QVector<McpTransport> &McpServerConfig::allTransports() {
    static const QVector<McpTransport> transports{
        McpTransport::StreamableHttp,
        McpTransport::Stdio,
        McpTransport::HttpSse,
    };
    return transports;
}

QString McpServerConfig::permissionName(McpPermission permission) {
    switch (permission) {
    case McpPermission::Off:
        return QStringLiteral("Off");
    case McpPermission::Ask:
        return QStringLiteral("Ask");
    case McpPermission::Allow:
        return QStringLiteral("Allow");
    }
    return QStringLiteral("unknown");
}

QString McpServerConfig::permissionKey(McpPermission permission) {
    switch (permission) {
    case McpPermission::Off:
        return QStringLiteral("off");
    case McpPermission::Ask:
        return QStringLiteral("ask");
    case McpPermission::Allow:
        return QStringLiteral("allow");
    }
    return QStringLiteral("ask");
}

McpPermission McpServerConfig::permissionFromString(const QString &key) {
    if (key == QLatin1String("off"))
        return McpPermission::Off;
    if (key == QLatin1String("allow"))
        return McpPermission::Allow;
    return McpPermission::Ask;
}

const QVector<McpPermission> &McpServerConfig::allPermissions() {
    static const QVector<McpPermission> permissions{
        McpPermission::Off,
        McpPermission::Ask,
        McpPermission::Allow,
    };
    return permissions;
}

} // namespace ocapi
