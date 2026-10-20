#include "core/mcpregistry.h"

#include "storage/chatdatabase.h"

namespace ocapi {

McpRegistry::McpRegistry(QObject *parent) : QObject(parent) {}

void McpRegistry::load(ChatDatabase &database) {
    m_servers = database.loadMcpServers();
    attachDatabase(&database);
}

void McpRegistry::attachDatabase(ChatDatabase *database) {
    m_database = database;
}

void McpRegistry::save() const {
    if (!m_database)
        return;
    for (const auto &config : m_servers)
        m_database->insertMcpServer(config);
}

McpServerConfig &McpRegistry::add(const QString &name, McpTransport transport) {
    McpServerConfig config;
    config.name = name;
    config.transport = transport;
    m_servers.push_back(std::move(config));
    persistToDatabase(m_servers.last());
    emit changed();
    return m_servers.last();
}

bool McpRegistry::remove(const QUuid &id) {
    for (auto it = m_servers.begin(); it != m_servers.end(); ++it) {
        if (it->id == id) {
            if (m_database)
                m_database->deleteMcpServer(id.toString(QUuid::WithoutBraces));
            m_servers.erase(it);
            emit changed();
            return true;
        }
    }
    return false;
}

void McpRegistry::persistToDatabase(const McpServerConfig &config) const {
    if (m_database)
        m_database->insertMcpServer(config);
}

McpServerConfig *McpRegistry::find(const QUuid &id) {
    for (auto &config : m_servers) {
        if (config.id == id)
            return &config;
    }
    return nullptr;
}

QVector<McpServerConfig> McpRegistry::enabled() const {
    QVector<McpServerConfig> result;
    for (const auto &config : m_servers) {
        if (config.enabled)
            result.push_back(config);
    }
    return result;
}

McpPermission McpRegistry::permissionForTool(const QUuid &serverId, const QString &toolName) const {
    for (const auto &config : m_servers) {
        if (config.id == serverId)
            return config.permissionForTool(toolName);
    }
    return McpPermission::Off;
}

void McpRegistry::setPermissionForTool(const QUuid &serverId, const QString &toolName,
                                       McpPermission permission) {
    auto *config = find(serverId);
    if (!config)
        return;
    config->setPermissionForTool(toolName, permission);
    persistToDatabase(*config);
    emit changed();
}

} // namespace ocapi
