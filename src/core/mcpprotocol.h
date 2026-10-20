#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

// JSON-RPC 2.0 / MCP protocol version implemented by the client. Bumped when
// newer MCP revisions add features the client can use.
inline constexpr auto kMcpProtocolVersion = "2025-06-18";

struct McpTool {
    QString name;
    QString description;
    QJsonObject inputSchema;
    QString serverId; // owning server, without braces
};

struct McpResource {
    QString uri;
    QString name;
    QString description;
    QString mimeType;
    QString serverId;
};

struct McpResourceTemplate {
    QString uriTemplate;
    QString name;
    QString description;
    QString mimeType;
    QString serverId;
};

struct McpPromptArgument {
    QString name;
    QString description;
    bool required = false;
};

struct McpPrompt {
    QString name;
    QString description;
    QString title;
    QVector<McpPromptArgument> arguments;
    QString serverId;
};

struct McpPromptMessage {
    QString role; // "user" or "assistant"
    QString text;
};

struct McpPromptContent {
    QVector<McpPromptMessage> messages;
};

struct McpCapabilities {
    bool tools = false;
    bool resources = false;
    bool prompts = false;
    bool logging = false;
    bool sampling = false;
};

struct McpServerInfo {
    QString name;
    QString version;
    QString title;
    McpCapabilities capabilities;
};

struct McpToolResult {
    bool isError = false;
    QString text;
    QJsonArray content;
};

// Builds a namespaced tool name ("serverName__toolName") as recommended by the
// MCP specification for clients that expose tools of multiple servers.
[[nodiscard]] inline QString namespacedToolName(const QString &serverName,
                                                const QString &toolName) {
    return serverName + QStringLiteral("__") + toolName;
}

[[nodiscard]] inline QJsonObject toolToJson(const McpTool &tool) {
    return {
        {QStringLiteral("name"), tool.name},
        {QStringLiteral("description"), tool.description},
        {QStringLiteral("inputSchema"), tool.inputSchema},
    };
}

[[nodiscard]] inline QJsonArray toolsToJson(const QVector<McpTool> &tools) {
    QJsonArray array;
    for (const auto &tool : tools)
        array.append(toolToJson(tool));
    return array;
}

[[nodiscard]] inline McpTool toolFromJson(const QJsonObject &object, const QString &serverId) {
    McpTool tool;
    tool.name = object.value(QStringLiteral("name")).toString();
    tool.description = object.value(QStringLiteral("description")).toString();
    tool.inputSchema = object.value(QStringLiteral("inputSchema")).toObject();
    tool.serverId = serverId;
    return tool;
}

[[nodiscard]] inline McpResource resourceFromJson(const QJsonObject &object,
                                                  const QString &serverId) {
    McpResource resource;
    resource.uri = object.value(QStringLiteral("uri")).toString();
    resource.name = object.value(QStringLiteral("name")).toString();
    resource.description = object.value(QStringLiteral("description")).toString();
    resource.mimeType = object.value(QStringLiteral("mimeType")).toString();
    resource.serverId = serverId;
    return resource;
}

[[nodiscard]] inline McpResourceTemplate resourceTemplateFromJson(const QJsonObject &object,
                                                                  const QString &serverId) {
    McpResourceTemplate templ;
    templ.uriTemplate = object.value(QStringLiteral("uriTemplate")).toString();
    templ.name = object.value(QStringLiteral("name")).toString();
    templ.description = object.value(QStringLiteral("description")).toString();
    templ.mimeType = object.value(QStringLiteral("mimeType")).toString();
    templ.serverId = serverId;
    return templ;
}

[[nodiscard]] inline McpPrompt promptFromJson(const QJsonObject &object, const QString &serverId) {
    McpPrompt prompt;
    prompt.name = object.value(QStringLiteral("name")).toString();
    prompt.description = object.value(QStringLiteral("description")).toString();
    prompt.title = object.value(QStringLiteral("title")).toString();
    prompt.serverId = serverId;
    const auto arguments = object.value(QStringLiteral("arguments")).toArray();
    prompt.arguments.reserve(arguments.size());
    for (const auto &value : arguments) {
        const auto argument = value.toObject();
        McpPromptArgument entry;
        entry.name = argument.value(QStringLiteral("name")).toString();
        entry.description = argument.value(QStringLiteral("description")).toString();
        entry.required = argument.value(QStringLiteral("required")).toBool();
        prompt.arguments.push_back(std::move(entry));
    }
    return prompt;
}

[[nodiscard]] inline McpCapabilities capabilitiesFromJson(const QJsonObject &object) {
    McpCapabilities capabilities;
    capabilities.tools = object.contains(QStringLiteral("tools"));
    capabilities.resources = object.contains(QStringLiteral("resources"));
    capabilities.prompts = object.contains(QStringLiteral("prompts"));
    capabilities.logging = object.contains(QStringLiteral("logging"));
    capabilities.sampling = object.contains(QStringLiteral("sampling"));
    return capabilities;
}

[[nodiscard]] inline McpServerInfo serverInfoFromJson(const QJsonObject &object) {
    McpServerInfo info;
    const auto serverInfo = object.value(QStringLiteral("serverInfo")).toObject();
    info.name = serverInfo.value(QStringLiteral("name")).toString();
    info.version = serverInfo.value(QStringLiteral("version")).toString();
    info.title = serverInfo.value(QStringLiteral("title")).toString();
    info.capabilities =
        capabilitiesFromJson(object.value(QStringLiteral("capabilities")).toObject());
    return info;
}

[[nodiscard]] inline QJsonObject clientCapabilitiesJson() {
    // The client supports elicitation (structured input from the user) and
    // roots, and never issues sampling requests to servers.
    return {
        {QStringLiteral("elicitation"), QJsonObject{{QStringLiteral("form"), QJsonObject{}}}},
        {QStringLiteral("roots"), QJsonObject{{QStringLiteral("listChanged"), true}}},
    };
}

[[nodiscard]] inline QJsonObject initializeParamsJson() {
    return {
        {QStringLiteral("protocolVersion"), QString::fromLatin1(kMcpProtocolVersion)},
        {QStringLiteral("capabilities"), clientCapabilitiesJson()},
        {QStringLiteral("clientInfo"),
         QJsonObject{
             {QStringLiteral("name"), QStringLiteral("ocapi")},
             {QStringLiteral("version"), QStringLiteral("0.1.0")},
         }},
    };
}

[[nodiscard]] inline QJsonObject toolCallToJson(const QString &name, const QJsonObject &arguments) {
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("arguments"), arguments},
    };
}

[[nodiscard]] inline McpToolResult toolResultFromJson(const QJsonObject &object) {
    McpToolResult result;
    result.isError = object.value(QStringLiteral("isError")).toBool();
    const auto content = object.value(QStringLiteral("content")).toArray();
    result.content = content;
    QStringList texts;
    texts.reserve(content.size());
    for (const auto &value : content) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
            texts.append(item.value(QStringLiteral("text")).toString());
    }
    result.text = texts.join(QStringLiteral("\n"));
    return result;
}

[[nodiscard]] inline QString toolResultToMessageContent(const McpToolResult &result) {
    if (!result.text.isEmpty())
        return result.text;
    return QString::fromUtf8(QJsonDocument(result.content).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline QJsonObject jsonRpcRequest(const QString &method, const QJsonObject &params,
                                                const QJsonValue &id) {
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
}

[[nodiscard]] inline QJsonObject jsonRpcNotification(const QString &method,
                                                     const QJsonObject &params) {
    return {
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
}

[[nodiscard]] inline bool isJsonRpcError(const QJsonObject &message) {
    return message.contains(QStringLiteral("error")) &&
           !message.value(QStringLiteral("error")).isNull();
}

[[nodiscard]] inline QString jsonRpcErrorText(const QJsonObject &message) {
    const auto error = message.value(QStringLiteral("error")).toObject();
    const QString text = error.value(QStringLiteral("message")).toString();
    const int code = error.value(QStringLiteral("code")).toInt();
    if (text.isEmpty())
        return QStringLiteral("JSON-RPC error %1").arg(code);
    return QStringLiteral("%1 (code %2)").arg(text).arg(code);
}

[[nodiscard]] inline QJsonValue jsonRpcIdFromMessage(const QJsonObject &message) {
    return message.value(QStringLiteral("id"));
}

[[nodiscard]] inline QString jsonRpcMethodFromMessage(const QJsonObject &message) {
    return message.value(QStringLiteral("method")).toString();
}

[[nodiscard]] inline QJsonObject jsonRpcParamsFromMessage(const QJsonObject &message) {
    return message.value(QStringLiteral("params")).toObject();
}

// Generates a unique request id for JSON-RPC calls.
[[nodiscard]] inline QJsonValue nextRequestId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace ocapi

Q_DECLARE_METATYPE(ocapi::McpTool)
Q_DECLARE_METATYPE(QVector<ocapi::McpTool>)
Q_DECLARE_METATYPE(ocapi::McpResource)
Q_DECLARE_METATYPE(QVector<ocapi::McpResource>)
Q_DECLARE_METATYPE(ocapi::McpResourceTemplate)
Q_DECLARE_METATYPE(QVector<ocapi::McpResourceTemplate>)
Q_DECLARE_METATYPE(ocapi::McpPrompt)
Q_DECLARE_METATYPE(QVector<ocapi::McpPrompt>)
Q_DECLARE_METATYPE(ocapi::McpToolResult)
Q_DECLARE_METATYPE(ocapi::McpServerInfo)
Q_DECLARE_METATYPE(ocapi::McpPromptContent)
Q_DECLARE_METATYPE(QVector<ocapi::ToolCall>)
