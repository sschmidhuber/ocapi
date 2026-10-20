#pragma once

#include "core/mcpprotocol.h"
#include "core/mcpregistry.h"

#include <QHash>
#include <QObject>
#include <QUuid>
#include <QVector>

class QJsonArray;

namespace ocapi {

class ChatSession;
class McpClient;

// Owns the MCP server connections for the application, exposes the union of
// all servers' tools as OpenAI-style function tools, and executes the
// tool-call loop: when the model requests tool calls, the manager runs them
// (honoring per-tool permissions) and feeds the results back.
class McpManager : public QObject {
    Q_OBJECT

public:
    explicit McpManager(McpRegistry *registry, QObject *parent = nullptr);
    ~McpManager() override;

    [[nodiscard]] McpRegistry *registry() const { return m_registry; }

    // (Re)connects all enabled servers. Called at startup and whenever the
    // registry changes.
    void reload();

    // All tools of all connected, enabled servers, in OpenAI tool format.
    [[nodiscard]] QJsonArray availableTools() const;
    // Flat list of tools with their owning server, for the settings UI.
    [[nodiscard]] QVector<McpTool> allTools() const { return m_tools; }

    [[nodiscard]] bool hasConnectedServers() const;

    // Executes one tool call after checking permissions. Emits
    // permissionRequested when the permission mode is Ask; the caller must
    // then invoke decidePermission(). Off/disabled calls fail immediately.
    // Returns false when the call cannot proceed without user input.
    bool executeToolCall(ChatSession *session, const ToolCall &call);

    // Answers a pending permission request. Safe to call only after
    // permissionRequested was emitted for this call.
    void decidePermission(const QString &callKey, bool allowOnce);

    // Maps a namespaced tool name ("server__tool") back to server id + name.
    [[nodiscard]] bool splitToolName(const QString &namespacedName, QUuid &serverId,
                                     QString &toolName) const;

signals:
    void toolsChanged();
    void serverStateChanged(const QString &serverId, const QString &serverName, bool connected);
    void serverLog(const QString &serverName, const QString &level, const QString &message);
    // Ask the user: allow this one call (allowOnce) or deny it?
    void permissionRequested(const QString &callKey, const QString &serverName,
                             const QString &toolName, const QJsonObject &arguments);
    // The tool result was appended to the session as a Role::Tool message.
    void toolResultReady(const QString &toolName, const QString &resultText, bool isError);
    void toolCallFailed(const QString &toolName, const QString &error);

private slots:
    void onRegistryChanged();

private:
    struct ServerEntry {
        McpClient *client = nullptr;
        QVector<McpTool> tools;
        QVector<McpResource> resources;
        QVector<McpResourceTemplate> resourceTemplates;
        QVector<McpPrompt> prompts;
    };

    struct PendingPermission {
        ChatSession *session = nullptr;
        ToolCall call;
        QString serverName;
    };

    struct PendingCall {
        ChatSession *session = nullptr;
        ToolCall call;
        QString namespacedToolName;
    };

    void connectServer(const McpServerConfig &config);
    void rebuildToolCache();
    void onToolResult(ServerEntry &entry, const QString &toolName, const McpToolResult &result);
    void appendToolResultMessage(ChatSession *session, const ToolCall &call, const QString &text,
                                 bool isError);

    McpRegistry *m_registry;
    QHash<QString, ServerEntry> m_servers;                  // server id (no braces) -> entry
    QVector<McpTool> m_tools;                               // flattened cache, namespaced names
    QHash<QString, PendingPermission> m_pendingPermissions; // callKey -> request
    QHash<QString, PendingCall> m_pendingCalls;             // callKey -> in-flight call
    bool m_reloading = false;
};

} // namespace ocapi
