#pragma once

#include "core/mcpserverconfig.h"

#include <QObject>
#include <QVector>

namespace ocapi {

class ChatDatabase;

// Holds the configured MCP servers and persists them through ChatDatabase,
// mirroring BackendRegistry for LLM backends.
class McpRegistry : public QObject {
    Q_OBJECT
public:
    explicit McpRegistry(QObject *parent = nullptr);

    void load(ChatDatabase &database);
    void attachDatabase(ChatDatabase *database);
    void save() const;

    [[nodiscard]] const QVector<McpServerConfig> &servers() const { return m_servers; }
    McpServerConfig &add(const QString &name, McpTransport transport);
    bool remove(const QUuid &id);
    McpServerConfig *find(const QUuid &id);
    [[nodiscard]] McpServerConfig *find(const QString &id) { return find(QUuid(id)); }
    [[nodiscard]] QVector<McpServerConfig> enabled() const;

    // Effective permission for a tool of a server, honoring server-wide
    // defaults and per-tool overrides.
    [[nodiscard]] McpPermission permissionForTool(const QUuid &serverId,
                                                  const QString &toolName) const;
    void setPermissionForTool(const QUuid &serverId, const QString &toolName,
                              McpPermission permission);

signals:
    void changed();

private:
    void persistToDatabase(const McpServerConfig &config) const;

    QVector<McpServerConfig> m_servers;
    ChatDatabase *m_database = nullptr;
};

} // namespace ocapi
