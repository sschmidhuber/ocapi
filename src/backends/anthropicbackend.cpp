#include "backends/anthropicbackend.h"
#include "backends/sseclient.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace ocapi {

namespace {

QUrl endpoint(const BackendConfig &config, const QString &path) {
    QUrl url(config.effectiveBaseUrl());
    QString basePath = url.path();
    if (!basePath.endsWith('/'))
        basePath.append('/');
    url.setPath(basePath + path);
    return url;
}

QStringList fallbackModels() {
    return {
        QStringLiteral("claude-sonnet-4-5"),
        QStringLiteral("claude-opus-4-1"),
        QStringLiteral("claude-haiku-4-5"),
    };
}

QNetworkRequest makeRequest(const QUrl &url, const QString &apiKey) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    return request;
}

// The MCP layer offers tools in OpenAI shape ({type, function: {name,
// description, parameters}}); the Anthropic API wants {name, description,
// input_schema}.
QJsonArray anthropicTools(const QJsonArray &openAiTools) {
    QJsonArray out;
    for (const auto &value : openAiTools) {
        const auto function = value.toObject().value(QStringLiteral("function")).toObject();
        QJsonObject schema = function.value(QStringLiteral("parameters")).toObject();
        if (schema.isEmpty())
            schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{}}};
        out.append(QJsonObject{
            {QStringLiteral("name"), function.value(QStringLiteral("name"))},
            {QStringLiteral("description"), function.value(QStringLiteral("description"))},
            {QStringLiteral("input_schema"), schema},
        });
    }
    return out;
}

} // namespace

AnthropicBackend::AnthropicBackend(BackendConfig config, QObject *parent)
    : IBackend(std::move(config), parent), m_nam(new QNetworkAccessManager(this)) {}

AnthropicBackend::~AnthropicBackend() = default;

void AnthropicBackend::listModels() {
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->deleteLater();
    }
    m_modelsReply =
        m_nam->get(makeRequest(endpoint(m_config, QStringLiteral("models")), m_config.apiKey));
    connect(m_modelsReply, &QNetworkReply::finished, this, [this]() {
        auto *reply = m_modelsReply;
        m_modelsReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Fall back to the well-known models when listing is unavailable
            // (older API versions, restricted keys or network issues).
            emit modelsReady(fallbackModels());
            return;
        }
        const auto data = QJsonDocument::fromJson(reply->readAll())
                              .object()
                              .value(QStringLiteral("data"))
                              .toArray();
        QStringList names;
        names.reserve(data.size());
        for (const auto &value : data)
            names.append(value.toObject().value(QStringLiteral("id")).toString());
        if (names.isEmpty())
            names = fallbackModels();
        emit modelsReady(names);
    });
}

void AnthropicBackend::sendMessage(ChatSession *session, const QString &model,
                                   const QUuid &continueMessageId) {
    cancel();
    // The assistant message is streaming from the moment it exists: the
    // reply is being generated even before the first token arrives (reasoning
    // models can think for a long time), and the chat must not yet show the
    // copy button that belongs to a finished message.
    m_currentMessageId =
        beginAssistantMessage(session, continueMessageId, m_accumulated, m_accumulatedReasoning);
    m_pendingToolCalls.clear();
    m_argumentsBuffer.clear();

    QString systemPrompt;
    QJsonArray messages;
    for (const auto &message : session->messages()) {
        // Skip the assistant message currently being generated only while it is
        // still empty (initial send); once it carries tool calls it must be
        // part of the conversation, and tool-result messages appended after
        // it must stay in the conversation as well.
        if (message.id == m_currentMessageId && message.content.isEmpty() &&
            !message.hasToolCalls())
            continue;
        if (!message.error.isEmpty())
            continue;
        if (message.role == Role::System) {
            systemPrompt.append(message.content).append(QLatin1Char('\n'));
            continue;
        }
        // Tool results travel as user-role tool_result blocks keyed by the
        // tool_use id of the requesting call.
        if (message.role == Role::Tool) {
            const QString text = message.textForModel();
            messages.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("tool_result")},
                     {QStringLiteral("tool_use_id"), message.toolCallId},
                     {QStringLiteral("content"), text},
                 }}},
            });
            continue;
        }
        QJsonArray contentParts;
        const QString text = message.textForModel();
        if (!text.isEmpty())
            contentParts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                            {QStringLiteral("text"), text}});
        for (const auto &image : message.images) {
            contentParts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image")},
                {QStringLiteral("source"),
                 QJsonObject{
                     {QStringLiteral("type"), QStringLiteral("base64")},
                     {QStringLiteral("media_type"), image.mimeType},
                     {QStringLiteral("data"), image.base64Data()},
                 }},
            });
        }
        // Replay the assistant's earlier tool calls as tool_use blocks so the
        // model sees what it already invoked.
        for (const auto &call : message.toolCalls) {
            contentParts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("tool_use")},
                {QStringLiteral("id"), call.id},
                {QStringLiteral("name"), call.name},
                {QStringLiteral("input"), call.arguments},
            });
        }
        if (contentParts.isEmpty())
            continue;
        messages.append(QJsonObject{
            {QStringLiteral("role"), message.role == Role::Assistant ? QStringLiteral("assistant")
                                                                     : QStringLiteral("user")},
            {QStringLiteral("content"), contentParts},
        });
    }
    systemPrompt = systemPrompt.trimmed();

    debugLogRequest(QStringLiteral("AnthropicBackend"), messages);
    QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("max_tokens"), 4096},
        {QStringLiteral("stream"), true},
    };
    if (!systemPrompt.isEmpty())
        payload.insert(QStringLiteral("system"), systemPrompt);
    // Some local/small models lose conversation-history recall when tool
    // definitions are present, so offering tools can be disabled per backend.
    if (m_toolsEnabled && m_config.toolsEnabled)
        payload.insert(QStringLiteral("tools"), anthropicTools(m_availableTools));

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &event, const QByteArray &data) {
                onStreamEvent(QString::fromUtf8(event), data);
            });
    connect(m_stream.get(), &SseClient::httpError, this, [this](int, const QByteArray &body) {
        const auto error = QJsonDocument::fromJson(body)
                               .object()
                               .value(QStringLiteral("error"))
                               .toObject()
                               .value(QStringLiteral("message"))
                               .toString();
        emit failed(m_currentMessageId, error.isEmpty() ? QString::fromUtf8(body) : error);
    });
    connect(m_stream.get(), &SseClient::networkError, this,
            [this](const QString &error) { emit failed(m_currentMessageId, error); });

    m_stream->post(makeRequest(endpoint(m_config, QStringLiteral("messages")), m_config.apiKey),
                   QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void AnthropicBackend::onStreamEvent(const QString &eventName, const QByteArray &data) {
    const auto object = QJsonDocument::fromJson(data).object();
    if (eventName == QStringLiteral("content_block_start")) {
        // A tool_use block starts with its id and name; arguments stream as
        // input_json_delta fragments afterwards.
        const QJsonObject block = object.value(QStringLiteral("content_block")).toObject();
        const int index = object.value(QStringLiteral("index")).toInt();
        if (block.value(QStringLiteral("type")).toString() == QStringLiteral("tool_use")) {
            while (m_pendingToolCalls.size() <= index)
                m_pendingToolCalls.append(ToolCall{});
            m_pendingToolCalls[index].id = block.value(QStringLiteral("id")).toString();
            m_pendingToolCalls[index].name = block.value(QStringLiteral("name")).toString();
        }
    } else if (eventName == QStringLiteral("content_block_delta")) {
        const int index = object.value(QStringLiteral("index")).toInt();
        const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QStringLiteral("input_json_delta")) {
            m_argumentsBuffer[index].append(delta.value(QStringLiteral("partial_json")).toString());
        } else {
            // Extended-thinking models stream their reasoning as
            // thinking_delta events before the answer text arrives.
            const QString thinking = delta.value(QStringLiteral("thinking")).toString();
            if (!thinking.isEmpty()) {
                m_accumulatedReasoning.append(thinking);
                emit reasoningReceived(m_currentMessageId, m_accumulatedReasoning);
            }
            const QString token = delta.value(QStringLiteral("text")).toString();
            m_accumulated.append(token);
            if (!token.isEmpty())
                emit tokenReceived(m_currentMessageId, m_accumulated);
        }
    } else if (eventName == QStringLiteral("message_stop")) {
        finishToolCallsIfComplete();
        emit finished(m_currentMessageId);
    } else if (eventName == QStringLiteral("error")) {
        emit failed(m_currentMessageId, object.value(QStringLiteral("message")).toString());
    }
}

void AnthropicBackend::finishToolCallsIfComplete() {
    if (m_pendingToolCalls.isEmpty())
        return;
    for (int i = 0; i < m_pendingToolCalls.size(); ++i) {
        m_pendingToolCalls[i].arguments =
            QJsonDocument::fromJson(m_argumentsBuffer.value(i).toUtf8()).object();
        if (m_pendingToolCalls[i].id.isEmpty())
            m_pendingToolCalls[i].id = QStringLiteral("toolu_%1").arg(i);
    }
    emit toolCallsRequested(m_currentMessageId, m_pendingToolCalls);
    m_pendingToolCalls.clear();
    m_argumentsBuffer.clear();
    m_accumulated.clear();
}

void AnthropicBackend::setAvailableTools(const QJsonArray &tools) {
    m_toolsEnabled = !tools.isEmpty();
    m_availableTools = tools;
}

void AnthropicBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

} // namespace ocapi
