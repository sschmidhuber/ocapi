#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace okapi {

// How the client reaches an MCP server.
enum class McpTransport {
    Stdio,          // spawn a local process, speak JSON-RPC over stdin/stdout
    StreamableHttp, // HTTP POST with JSON responses or SSE streams (MCP 2025-03-26+)
    HttpSse,        // legacy HTTP+SSE transport (POST messages, GET event stream)
};

// Per-tool permission: what the user allows the agent to do without asking.
enum class McpPermission {
    Off,   // tool (or whole server) is disabled
    Ask,   // ask the user before every use
    Allow, // use without asking
};

struct McpServerConfig {
    QUuid id = QUuid::createUuid();
    QString name;
    McpTransport transport = McpTransport::StreamableHttp;
    // StreamableHttp / HttpSse: endpoint URL. Stdio: command to execute.
    QString url;
    QString command;
    QStringList arguments;
    // Environment variables for stdio servers, "KEY=VALUE" per entry.
    QStringList environment;
    // API key for HTTP transports, sent as "Authorization: Bearer <key>"
    // unless the headers list already sets an Authorization header.
    QString apiKey;
    // Extra HTTP headers, "Name: value" per entry (HTTP transports only).
    QStringList headers;
    // Server-wide default permission; per-tool overrides live in toolPermissions.
    McpPermission defaultPermission = McpPermission::Ask;
    // tool name -> permission override
    QJsonObject toolPermissions;
    bool enabled = true;

    [[nodiscard]] static QString transportName(McpTransport transport);
    [[nodiscard]] static QString transportKey(McpTransport transport);
    [[nodiscard]] static McpTransport transportFromString(const QString &key);
    [[nodiscard]] static const QVector<McpTransport> &allTransports();
    [[nodiscard]] QString transportName() const { return transportName(transport); }

    // Headers actually sent to the server: the custom headers plus
    // "Authorization: Bearer <apiKey>" when an API key is set and no custom
    // Authorization header overrides it.
    [[nodiscard]] QStringList effectiveHeaders() const;

    [[nodiscard]] static QString permissionName(McpPermission permission);
    [[nodiscard]] static QString permissionKey(McpPermission permission);
    [[nodiscard]] static McpPermission permissionFromString(const QString &key);
    [[nodiscard]] static const QVector<McpPermission> &allPermissions();

    // Effective permission for a tool: per-tool override wins over the
    // server-wide default; a disabled server forces Off.
    [[nodiscard]] McpPermission permissionForTool(const QString &toolName) const {
        if (!enabled)
            return McpPermission::Off;
        const auto it = toolPermissions.find(toolName);
        if (it != toolPermissions.end())
            return permissionFromString(it->toString());
        return defaultPermission;
    }

    void setPermissionForTool(const QString &toolName, McpPermission permission) {
        toolPermissions.insert(toolName, permissionKey(permission));
    }
};

} // namespace okapi
