#include "backends/geminibackend.h"
#include "backends/sseclient.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace ocapi {

namespace {

QString geminiRole(Role role) {
    switch (role) {
    case Role::Assistant:
        return QStringLiteral("model");
    case Role::System:
    case Role::Tool:
    case Role::User:
    default:
        return QStringLiteral("user");
    }
}

// The MCP layer offers tools in OpenAI shape ({type, function: {name,
// description, parameters}}); the Gemini API wants a tools array of
// functionDeclarations.
QJsonArray geminiTools(const QJsonArray &openAiTools) {
    QJsonArray declarations;
    for (const auto &value : openAiTools) {
        const auto function = value.toObject().value(QStringLiteral("function")).toObject();
        QJsonObject parameters = function.value(QStringLiteral("parameters")).toObject();
        if (parameters.isEmpty())
            parameters = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                     {QStringLiteral("properties"), QJsonObject{}}};
        declarations.append(QJsonObject{
            {QStringLiteral("name"), function.value(QStringLiteral("name"))},
            {QStringLiteral("description"), function.value(QStringLiteral("description"))},
            {QStringLiteral("parameters"), parameters},
        });
    }
    return QJsonArray{QJsonObject{{QStringLiteral("functionDeclarations"), declarations}}};
}

} // namespace

QStringList fallbackModels() {
    return {
        QStringLiteral("gemini-2.5-pro"),
        QStringLiteral("gemini-2.5-flash"),
        QStringLiteral("gemini-2.0-flash"),
    };
}

GeminiBackend::GeminiBackend(BackendConfig config, QObject *parent)
    : IBackend(std::move(config), parent), m_nam(new QNetworkAccessManager(this)) {}

GeminiBackend::~GeminiBackend() = default;

void GeminiBackend::listModels() {
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->deleteLater();
    }
    QUrl url(m_config.effectiveBaseUrl() + QStringLiteral("/models"));
    if (!m_config.apiKey.isEmpty())
        url.setQuery(QUrlQuery({{QStringLiteral("key"), m_config.apiKey}}));
    m_modelsReply = m_nam->get(QNetworkRequest(url));
    connect(m_modelsReply, &QNetworkReply::finished, this, [this]() {
        auto *reply = m_modelsReply;
        m_modelsReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Fall back to the well-known models when listing is unavailable
            // (invalid key, restricted access or network issues).
            emit modelsReady(fallbackModels());
            return;
        }
        const auto models = QJsonDocument::fromJson(reply->readAll())
                                .object()
                                .value(QStringLiteral("models"))
                                .toArray();
        QStringList names;
        names.reserve(models.size());
        for (const auto &value : models) {
            // Model ids look like "models/gemini-2.0-flash"; strip the prefix.
            QString name = value.toObject().value(QStringLiteral("name")).toString();
            if (name.startsWith(QStringLiteral("models/")))
                name.remove(0, 7);
            names.append(name);
        }
        if (names.isEmpty())
            names = fallbackModels();
        emit modelsReady(names);
    });
}

void GeminiBackend::sendMessage(ChatSession *session, const QString &model,
                                const QUuid &continueMessageId) {
    cancel();
    // The assistant message is streaming from the moment it exists: the
    // reply is being generated even before the first token arrives (reasoning
    // models can think for a long time), and the chat must not yet show the
    // copy button that belongs to a finished message.
    m_currentMessageId =
        beginAssistantMessage(session, continueMessageId, m_accumulated, m_accumulatedReasoning);
    m_pendingToolCalls.clear();

    QString systemPrompt;
    QJsonArray contents;
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
        // Tool results travel as user-role functionResponse parts.
        if (message.role == Role::Tool) {
            const QString text = message.textForModel();
            contents.append(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("parts"), QJsonArray{QJsonObject{
                                              {QStringLiteral("functionResponse"),
                                               QJsonObject{
                                                   {QStringLiteral("name"), message.toolName},
                                                   {QStringLiteral("response"),
                                                    QJsonObject{{QStringLiteral("result"), text}}},
                                               }},
                                          }}},
            });
            continue;
        }
        QJsonArray parts;
        const QString text = message.textForModel();
        if (!text.isEmpty())
            parts.append(QJsonObject{{QStringLiteral("text"), text}});
        for (const auto &image : message.images) {
            parts.append(QJsonObject{
                {QStringLiteral("inlineData"),
                 QJsonObject{
                     {QStringLiteral("mimeType"), image.mimeType},
                     {QStringLiteral("data"), image.base64Data()},
                 }},
            });
        }
        // Replay the model's earlier function calls as functionCall parts.
        for (const auto &call : message.toolCalls) {
            parts.append(QJsonObject{
                {QStringLiteral("functionCall"),
                 QJsonObject{
                     {QStringLiteral("name"), call.name},
                     {QStringLiteral("args"), call.arguments},
                 }},
            });
        }
        if (parts.isEmpty())
            continue;
        contents.append(QJsonObject{
            {QStringLiteral("role"), geminiRole(message.role)},
            {QStringLiteral("parts"), parts},
        });
    }
    systemPrompt = systemPrompt.trimmed();

    debugLogRequest(QStringLiteral("GeminiBackend"), contents);
    QJsonObject payload{
        {QStringLiteral("contents"), contents},
    };
    if (!systemPrompt.isEmpty()) {
        payload.insert(QStringLiteral("systemInstruction"),
                       QJsonObject{
                           {QStringLiteral("parts"), QJsonArray{QJsonObject{
                                                         {QStringLiteral("text"), systemPrompt},
                                                     }}},
                       });
    }
    // Some local/small models lose conversation-history recall when tool
    // definitions are present, so offering tools can be disabled per backend.
    if (m_toolsEnabled && m_config.toolsEnabled)
        payload.insert(QStringLiteral("tools"), geminiTools(m_availableTools));

    QUrl url(m_config.effectiveBaseUrl() + QStringLiteral("/models/") + model +
             QStringLiteral(":streamGenerateContent?alt=sse"));
    if (!m_config.apiKey.isEmpty())
        url.setQuery(QUrlQuery({{QStringLiteral("key"), m_config.apiKey}}));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &, const QByteArray &data) { onStreamEvent(data); });
    connect(m_stream.get(), &SseClient::httpError, this, [this](int, const QByteArray &body) {
        const auto message = QJsonDocument::fromJson(body)
                                 .object()
                                 .value(QStringLiteral("error"))
                                 .toObject()
                                 .value(QStringLiteral("message"))
                                 .toString();
        emit failed(m_currentMessageId, message.isEmpty() ? QString::fromUtf8(body) : message);
    });
    connect(m_stream.get(), &SseClient::networkError, this,
            [this](const QString &error) { emit failed(m_currentMessageId, error); });
    connect(m_stream.get(), &SseClient::finished, this,
            [this]() { emit finished(m_currentMessageId); });

    m_stream->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void GeminiBackend::onStreamEvent(const QByteArray &data) {
    const auto candidates =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty())
        return;
    const auto parts = candidates.first()
                           .toObject()
                           .value(QStringLiteral("content"))
                           .toObject()
                           .value(QStringLiteral("parts"))
                           .toArray();
    for (const auto &part : parts) {
        const QJsonObject partObject = part.toObject();
        // Thinking models mark their reasoning parts with
        // "thought": true (Gemini 2.5 stream/body style).
        if (partObject.value(QStringLiteral("thought")).toBool()) {
            m_accumulatedReasoning.append(partObject.value(QStringLiteral("text")).toString());
            emit reasoningReceived(m_currentMessageId, m_accumulatedReasoning);
            continue;
        }
        // Function calls arrive as complete functionCall parts (no deltas).
        const QJsonObject functionCall =
            partObject.value(QStringLiteral("functionCall")).toObject();
        if (!functionCall.isEmpty()) {
            ToolCall call;
            call.name = functionCall.value(QStringLiteral("name")).toString();
            call.arguments = functionCall.value(QStringLiteral("args")).toObject();
            m_pendingToolCalls.append(call);
            continue;
        }
        const QString token = partObject.value(QStringLiteral("text")).toString();
        m_accumulated.append(token);
    }
    emit tokenReceived(m_currentMessageId, m_accumulated);
    const bool done =
        !candidates.first().toObject().value(QStringLiteral("finishReason")).toString().isEmpty();
    if (done) {
        finishToolCallsIfComplete();
        emit finished(m_currentMessageId);
    }
}

void GeminiBackend::finishToolCallsIfComplete() {
    if (m_pendingToolCalls.isEmpty())
        return;
    for (int i = 0; i < m_pendingToolCalls.size(); ++i) {
        if (m_pendingToolCalls[i].id.isEmpty())
            m_pendingToolCalls[i].id = QStringLiteral("call_%1").arg(i);
    }
    emit toolCallsRequested(m_currentMessageId, m_pendingToolCalls);
    m_pendingToolCalls.clear();
    m_accumulated.clear();
}

void GeminiBackend::setAvailableTools(const QJsonArray &tools) {
    m_toolsEnabled = !tools.isEmpty();
    m_availableTools = tools;
}

void GeminiBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

} // namespace ocapi
