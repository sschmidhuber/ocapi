#include "backends/openaiapibackend.h"
#include "backends/contentparts.h"
#include "backends/sseclient.h"
#include "core/chatsession.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

QNetworkRequest makeRequest(const QUrl &url, const QString &apiKey) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    return request;
}

QJsonValue contentForMessage(const Message &message) {
    const QString text = message.textForModel();
    if (message.images.isEmpty())
        return text;
    QJsonArray parts;
    if (!text.isEmpty()) {
        parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                 {QStringLiteral("text"), text}});
    }
    for (const auto &image : message.images) {
        parts.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"),
             QJsonObject{
                 {QStringLiteral("url"),
                  QStringLiteral("data:%1;base64,%2").arg(image.mimeType, image.base64Data())}}},
        });
    }
    return parts;
}

QJsonObject assistantMessageToJson(const Message &message) {
    QJsonObject object{
        {QStringLiteral("role"), message.roleName()},
        {QStringLiteral("content"), message.content.isEmpty() ? QJsonValue() : message.content},
    };
    if (message.hasToolCalls()) {
        QJsonArray toolCalls;
        for (const auto &call : message.toolCalls) {
            toolCalls.append(QJsonObject{
                {QStringLiteral("id"), call.id},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{
                     {QStringLiteral("name"), call.name},
                     {QStringLiteral("arguments"),
                      QString::fromUtf8(
                          QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))},
                 }},
            });
        }
        object.insert(QStringLiteral("tool_calls"), toolCalls);
    }
    return object;
}

QJsonObject toolMessageToJson(const Message &message) {
    return {
        {QStringLiteral("role"), QStringLiteral("tool")},
        {QStringLiteral("tool_call_id"), message.toolCallId},
        {QStringLiteral("name"), message.toolName},
        {QStringLiteral("content"), message.content},
    };
}

} // namespace

OpenAiApiBackend::OpenAiApiBackend(BackendConfig config, QObject *parent)
    : IBackend(std::move(config), parent), m_nam(new QNetworkAccessManager(this)) {}

OpenAiApiBackend::~OpenAiApiBackend() = default;

void OpenAiApiBackend::listModels() {
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->deleteLater();
    }
    QNetworkRequest request =
        makeRequest(endpoint(m_config, QStringLiteral("models")), m_config.apiKey);
    m_modelsReply = m_nam->get(request);
    connect(m_modelsReply, &QNetworkReply::finished, this, [this]() {
        auto *reply = m_modelsReply;
        m_modelsReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit modelsFailed(reply->errorString());
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const auto data = doc.object().value(QStringLiteral("data")).toArray();
        QStringList names;
        names.reserve(data.size());
        for (const auto &value : data)
            names.append(value.toObject().value(QStringLiteral("id")).toString());
        emit modelsReady(names);
    });
}

namespace {

QJsonArray conversationToJson(const ChatSession *session, const QUuid &currentMessageId) {
    QJsonArray messages;
    for (const auto &message : session->messages()) {
        if (message.id == currentMessageId)
            break;
        if (!message.error.isEmpty())
            continue;
        if (message.role == Role::Assistant && message.hasToolCalls()) {
            messages.append(assistantMessageToJson(message));
            continue;
        }
        if (message.role == Role::Tool) {
            messages.append(toolMessageToJson(message));
            continue;
        }
        messages.append(QJsonObject{
            {QStringLiteral("role"), message.roleName()},
            {QStringLiteral("content"), contentForMessage(message)},
        });
    }
    return messages;
}

} // namespace

void OpenAiApiBackend::sendMessage(ChatSession *session, const QString &model) {
    cancel();
    m_currentMessageId = session->appendMessage(Role::Assistant).id;
    m_accumulated.clear();
    m_pendingToolCalls.clear();
    m_argumentsBuffer.clear();
    m_hasToolCalls = false;

    QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), conversationToJson(session, m_currentMessageId)},
        {QStringLiteral("stream"), true},
    };
    if (m_toolsEnabled) {
        payload.insert(QStringLiteral("tools"), m_availableTools);
        payload.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &, const QByteArray &data) { onStreamEvent(data); });
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
    connect(m_stream.get(), &SseClient::finished, this,
            [this]() { emit finished(m_currentMessageId); });

    m_stream->post(
        makeRequest(endpoint(m_config, QStringLiteral("chat/completions")), m_config.apiKey),
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

namespace {

QString contentText(const QJsonObject &delta) {
    return ocapiContentText(delta.value(QStringLiteral("content")));
}

} // namespace

void OpenAiApiBackend::onStreamEvent(const QByteArray &data) {
    const QString text = QString::fromUtf8(data);
    if (text == QStringLiteral("[DONE]"))
        return;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // Malformed events would otherwise be dropped silently, hiding stream
        // corruption from the user (e.g. the first tokens of a reply).
        qWarning() << "OpenAI-compatible stream: JSON parse error at offset" << parseError.offset
                   << ":" << parseError.errorString() << "in" << data;
        return;
    }
    const auto object = doc.object();
    const auto choices = object.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const auto choice = choices.first().toObject();
    const auto delta = choice.value(QStringLiteral("delta")).toObject();
    const QString token = contentText(delta);
    if (!token.isEmpty()) {
        m_accumulated.append(token);
        emit tokenReceived(m_currentMessageId, m_accumulated);
    }
    processToolCallDeltas(delta.value(QStringLiteral("tool_calls")).toArray());
    const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
    if (finishReason == QStringLiteral("stop") || finishReason == QStringLiteral("length") ||
        finishReason == QStringLiteral("tool_calls")) {
        finishIfToolCallsComplete();
        emit finished(m_currentMessageId);
    }
}

void OpenAiApiBackend::processToolCallDeltas(const QJsonArray &toolCallDeltas) {
    if (toolCallDeltas.isEmpty())
        return;
    for (const auto &value : toolCallDeltas) {
        const auto toolCallDelta = value.toObject();
        const int index = toolCallDelta.value(QStringLiteral("index")).toInt();
        while (m_pendingToolCalls.size() <= index)
            m_pendingToolCalls.append(ToolCall{});
        auto &call = m_pendingToolCalls[index];
        const QString id = toolCallDelta.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            call.id = id;
        const auto function = toolCallDelta.value(QStringLiteral("function")).toObject();
        call.name = function.value(QStringLiteral("name")).toString();
        m_argumentsBuffer[index].append(function.value(QStringLiteral("arguments")).toString());
    }
    m_hasToolCalls = true;
}

void OpenAiApiBackend::finishIfToolCallsComplete() {
    if (!m_hasToolCalls || m_pendingToolCalls.isEmpty())
        return;
    // Arguments arrive as JSON fragments; parse the accumulated buffer per call.
    for (int i = 0; i < m_pendingToolCalls.size(); ++i) {
        const QString buffer = m_argumentsBuffer.value(i);
        m_pendingToolCalls[i].arguments = QJsonDocument::fromJson(buffer.toUtf8()).object();
        if (m_pendingToolCalls[i].id.isEmpty())
            m_pendingToolCalls[i].id = QStringLiteral("call_%1").arg(i);
    }
    emit toolCallsRequested(m_currentMessageId, m_pendingToolCalls);
    m_pendingToolCalls.clear();
    m_argumentsBuffer.clear();
    m_hasToolCalls = false;
    m_accumulated.clear();
}

void OpenAiApiBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

bool OpenAiApiBackend::supportsTools() const {
    return true;
}

void OpenAiApiBackend::setAvailableTools(const QJsonArray &tools) {
    m_toolsEnabled = !tools.isEmpty();
    m_availableTools = tools;
}

} // namespace ocapi
