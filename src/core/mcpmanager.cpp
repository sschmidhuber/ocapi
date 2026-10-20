#include "core/mcpmanager.h"

#include "backends/mcpclient.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonObject>

namespace ocapi {

namespace {

QString serverIdString(const QUuid &id) {
    return id.toString(QUuid::WithoutBraces);
}

} // namespace

McpManager::McpManager(McpRegistry *registry, QObject *parent)
    : QObject(parent), m_registry(registry) {
    connect(m_registry, &McpRegistry::changed, this, &McpManager::onRegistryChanged);
}

McpManager::~McpManager() {
    for (auto &entry : m_servers)
        delete entry.client;
    m_servers.clear();
}

void McpManager::reload() {
    m_reloading = true;
    // Drop servers that were removed or disabled.
    const auto configured = m_registry->servers();
    for (auto it = m_servers.begin(); it != m_servers.end();) {
        const auto *config = m_registry->find(QUuid(it.key()));
        if (!config || !config->enabled) {
            delete it.value().client;
            it = m_servers.erase(it);
        } else {
            ++it;
        }
    }
    // (Re)connect enabled servers.
    for (const auto &config : configured) {
        if (!config.enabled)
            continue;
        const QString id = serverIdString(config.id);
        if (m_servers.contains(id)) {
            // Config may have changed; reconnect to apply it.
            delete m_servers[id].client;
            m_servers.remove(id);
        }
        connectServer(config);
    }
    m_reloading = false;
    rebuildToolCache();
}

void McpManager::onRegistryChanged() {
    if (!m_reloading)
        reload();
}

void McpManager::connectServer(const McpServerConfig &config) {
    const QString id = serverIdString(config.id);
    auto *client = new McpClient(config, this);
    ServerEntry entry;
    entry.client = client;
    m_servers.insert(id, entry);

    connect(client, &McpClient::toolsReady, this, [this, id](const QVector<McpTool> &tools) {
        auto it = m_servers.find(id);
        if (it == m_servers.end())
            return;
        it->tools = tools;
        rebuildToolCache();
    });
    connect(client, &McpClient::resourcesReady, this, [this, id](const QVector<McpResource> &r) {
        auto it = m_servers.find(id);
        if (it != m_servers.end())
            it->resources = r;
    });
    connect(client, &McpClient::resourceTemplatesReady, this,
            [this, id](const QVector<McpResourceTemplate> &t) {
                auto it = m_servers.find(id);
                if (it != m_servers.end())
                    it->resourceTemplates = t;
            });
    connect(client, &McpClient::promptsReady, this, [this, id](const QVector<McpPrompt> &p) {
        auto it = m_servers.find(id);
        if (it != m_servers.end())
            it->prompts = p;
    });
    connect(client, &McpClient::logMessage, this,
            [this, id](const QString &level, const QString &, const QString &message) {
                const auto *config = m_registry->find(QUuid(id));
                emit serverLog(config ? config->name : id, level, message);
            });
    connect(client, &McpClient::toolResult, this,
            [this, id](const QString &toolName, const McpToolResult &result) {
                auto it = m_servers.find(id);
                if (it != m_servers.end())
                    onToolResult(it.value(), toolName, result);
            });
    connect(client, &McpClient::stateChanged, this, [this, id](McpClient::State state) {
        if (state != McpClient::State::Connected && state != McpClient::State::Failed)
            return;
        const auto *config = m_registry->find(QUuid(id));
        emit serverStateChanged(id, config ? config->name : id,
                                state == McpClient::State::Connected);
    });
    connect(client, &McpClient::connectionFailed, this, [this, id](const QString &) {
        // Keep the entry so the UI can show the failure; tools stay empty.
        // Connection state is reported through McpClient::stateChanged.
        Q_UNUSED(id)
    });
    connect(client, &McpClient::requestFailed, this,
            [this, id](const QString &method, const QString &error) {
                if (method != QLatin1String("tools/call"))
                    return;
                const auto *config = m_registry->find(QUuid(id));
                emit toolCallFailed(config ? config->name : id, error);
            });

    client->connectToServer();
}

void McpManager::rebuildToolCache() {
    m_tools.clear();
    for (auto it = m_servers.begin(); it != m_servers.end(); ++it) {
        const auto *config = m_registry->find(QUuid(it.key()));
        const QString serverName = config ? config->name : it.key();
        for (const auto &tool : it->tools) {
            if (!config || !config->enabled)
                continue;
            McpTool namespaced = tool;
            namespaced.name = namespacedToolName(serverName, tool.name);
            m_tools.push_back(std::move(namespaced));
        }
    }
    emit toolsChanged();
}

QJsonArray McpManager::availableTools() const {
    QJsonArray tools;
    for (const auto &tool : m_tools) {
        tools.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"),
             QJsonObject{
                 {QStringLiteral("name"), tool.name},
                 {QStringLiteral("description"), tool.description},
                 {QStringLiteral("parameters"), tool.inputSchema},
             }},
        });
    }
    return tools;
}

bool McpManager::hasConnectedServers() const {
    for (const auto &entry : m_servers) {
        if (entry.client && entry.client->isConnected())
            return true;
    }
    return false;
}

bool McpManager::splitToolName(const QString &namespacedName, QUuid &serverId,
                               QString &toolName) const {
    // Names are "serverName__toolName"; match against known tools because the
    // server name itself may contain "__".
    for (const auto &tool : m_tools) {
        if (tool.name == namespacedName) {
            serverId = QUuid(tool.serverId);
            const auto *config = m_registry->find(serverId);
            const QString serverName = config ? config->name : QString();
            const QString prefix = serverName + QStringLiteral("__");
            if (namespacedName.startsWith(prefix)) {
                toolName = namespacedName.mid(prefix.size());
                return true;
            }
        }
    }
    const int separator = namespacedName.indexOf(QStringLiteral("__"));
    if (separator <= 0)
        return false;
    toolName = namespacedName.mid(separator + 2);
    for (const auto &entry : m_servers) {
        const auto *config = m_registry->find(QUuid(entry.key()));
        if (config && namespacedToolName(config->name, toolName) == namespacedName) {
            serverId = config->id;
            return true;
        }
    }
    return false;
}

bool McpManager::executeToolCall(ChatSession *session, const ToolCall &call) {
    QUuid serverId;
    QString toolName;
    if (!splitToolName(call.name, serverId, toolName)) {
        appendToolResultMessage(session, call, tr("Unknown MCP tool \"%1\".").arg(call.name), true);
        return true;
    }
    const auto permission = m_registry->permissionForTool(serverId, toolName);
    if (permission == McpPermission::Off) {
        appendToolResultMessage(session, call,
                                tr("Tool \"%1\" is disabled by the user.").arg(toolName), true);
        return true;
    }
    const auto *config = m_registry->find(serverId);
    if (!config) {
        appendToolResultMessage(
            session, call, tr("MCP server for tool \"%1\" is not configured.").arg(toolName), true);
        return true;
    }
    auto it = m_servers.find(serverIdString(serverId));
    if (it == m_servers.end() || !it->client || !it->client->isConnected()) {
        appendToolResultMessage(session, call,
                                tr("MCP server \"%1\" is not connected.").arg(config->name), true);
        return true;
    }
    if (permission == McpPermission::Ask) {
        const QString callKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pendingPermissions.insert(callKey, PendingPermission{session, call, config->name});
        emit permissionRequested(callKey, config->name, toolName, call.arguments);
        return false;
    }
    const QString callKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pendingCalls.insert(callKey, PendingCall{session, call, call.name});
    it->client->callTool(toolName, call.arguments);
    return true;
}

void McpManager::decidePermission(const QString &callKey, bool allowOnce) {
    const auto it = m_pendingPermissions.constFind(callKey);
    if (it == m_pendingPermissions.constEnd())
        return;
    const PendingPermission pending = it.value();
    m_pendingPermissions.erase(it);
    if (!allowOnce) {
        appendToolResultMessage(
            pending.session, pending.call,
            tr("Tool call \"%1\" was denied by the user.").arg(pending.call.name), true);
        return;
    }
    QUuid serverId;
    QString toolName;
    if (splitToolName(pending.call.name, serverId, toolName)) {
        auto entryIt = m_servers.find(serverIdString(serverId));
        if (entryIt != m_servers.end() && entryIt->client && entryIt->client->isConnected()) {
            const QString callKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_pendingCalls.insert(callKey,
                                  PendingCall{pending.session, pending.call, pending.call.name});
            entryIt->client->callTool(toolName, pending.call.arguments);
        }
    }
}

void McpManager::onToolResult(ServerEntry &entry, const QString &toolName,
                              const McpToolResult &result) {
    const QString serverId = entry.client ? serverIdString(entry.client->config().id) : QString();
    const QString namespacedName = [this, &serverId, &toolName]() {
        const auto *config = m_registry->find(QUuid(serverId));
        return namespacedToolName(config ? config->name : QString(), toolName);
    }();
    // Correlate with the oldest pending call for this tool.
    for (auto it = m_pendingCalls.begin(); it != m_pendingCalls.end(); ++it) {
        if (it->namespacedToolName == namespacedName) {
            const PendingCall pending = it.value();
            m_pendingCalls.erase(it);
            appendToolResultMessage(pending.session, pending.call,
                                    toolResultToMessageContent(result), result.isError);
            return;
        }
    }
    emit toolResultReady(namespacedName, toolResultToMessageContent(result), result.isError);
}

void McpManager::appendToolResultMessage(ChatSession *session, const ToolCall &call,
                                         const QString &text, bool isError) {
    if (!session)
        return;
    Message message;
    message.role = Role::Tool;
    message.toolCallId = call.id;
    message.toolName = call.name;
    message.content = text;
    session->appendMessage(message);
    emit toolResultReady(call.name, text, isError);
}

} // namespace ocapi
