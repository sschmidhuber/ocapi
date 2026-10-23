#include "backends/ollamabackend.h"
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

QNetworkRequest makeRequest(const QUrl &url, const QString &apiKey) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    return request;
}

QJsonObject toOllamaMessage(const Message &message) {
    QJsonObject object{
        {QStringLiteral("role"), message.roleName()},
        {QStringLiteral("content"), message.textForModel()},
    };
    if (!message.images.isEmpty()) {
        QJsonArray images;
        for (const auto &image : message.images)
            images.append(QString::fromLatin1(image.data.toBase64()));
        object.insert(QStringLiteral("images"), images);
    }
    if (message.role == Role::Assistant && message.hasToolCalls()) {
        QJsonArray toolCalls;
        for (const auto &call : message.toolCalls) {
            toolCalls.append(QJsonObject{
                {QStringLiteral("function"),
                 QJsonObject{
                     {QStringLiteral("name"), call.name},
                     {QStringLiteral("arguments"), call.arguments},
                 }},
            });
        }
        object.insert(QStringLiteral("tool_calls"), toolCalls);
    }
    return object;
}

} // namespace

OllamaBackend::OllamaBackend(BackendConfig config, QObject *parent)
    : IBackend(std::move(config), parent), m_nam(new QNetworkAccessManager(this)) {}

OllamaBackend::~OllamaBackend() = default;

void OllamaBackend::listModels() {
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->deleteLater();
    }
    QNetworkRequest request =
        makeRequest(endpoint(m_config, QStringLiteral("api/tags")), m_config.apiKey);
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
        const auto models = doc.object().value(QStringLiteral("models")).toArray();
        QStringList names;
        names.reserve(models.size());
        for (const auto &value : models)
            names.append(value.toObject().value(QStringLiteral("name")).toString());
        emit modelsReady(names);
    });
}

void OllamaBackend::sendMessage(ChatSession *session, const QString &model) {
    cancel();
    m_currentMessageId = session->appendMessage(Role::Assistant).id;
    m_accumulated.clear();

    QJsonArray messages;
    for (const auto &message : session->messages()) {
        if (message.id == m_currentMessageId)
            break;
        if (!message.error.isEmpty())
            continue;
        messages.append(toOllamaMessage(message));
    }

    QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
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
        emit failed(m_currentMessageId, QString::fromUtf8(body));
    });
    connect(m_stream.get(), &SseClient::networkError, this,
            [this](const QString &error) { emit failed(m_currentMessageId, error); });
    connect(m_stream.get(), &SseClient::finished, this,
            [this]() { emit finished(m_currentMessageId); });

    m_stream->post(makeRequest(endpoint(m_config, QStringLiteral("api/chat")), m_config.apiKey),
                   QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void OllamaBackend::onStreamEvent(const QByteArray &data) {
    const auto doc = QJsonDocument::fromJson(data);
    const auto object = doc.object();
    if (object.contains(QStringLiteral("error"))) {
        emit failed(m_currentMessageId, object.value(QStringLiteral("error")).toString());
        return;
    }
    const auto message = object.value(QStringLiteral("message")).toObject();

    // Tool calls end the turn; the agent layer runs the tools and sends the
    // results back with a new sendMessage() call.
    const auto toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    if (!toolCalls.isEmpty()) {
        QVector<ToolCall> calls;
        calls.reserve(toolCalls.size());
        for (const auto &value : toolCalls) {
            const auto toolCall = value.toObject();
            const auto function = toolCall.value(QStringLiteral("function")).toObject();
            ToolCall call;
            call.name = function.value(QStringLiteral("name")).toString();
            call.arguments = function.value(QStringLiteral("arguments")).toObject();
            calls.push_back(std::move(call));
        }
        emit toolCallsRequested(m_currentMessageId, calls);
        return;
    }

    const QString token = message.value(QStringLiteral("content")).toString();
    m_accumulated.append(token);
    emit tokenReceived(m_currentMessageId, m_accumulated);
    if (object.value(QStringLiteral("done")).toBool())
        emit finished(m_currentMessageId);
}

void OllamaBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

bool OllamaBackend::supportsTools() const {
    return true;
}

void OllamaBackend::setAvailableTools(const QJsonArray &tools) {
    m_toolsEnabled = !tools.isEmpty();
    m_availableTools = tools;
}

} // namespace ocapi
