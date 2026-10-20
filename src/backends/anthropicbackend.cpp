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

void AnthropicBackend::sendMessage(ChatSession *session, const QString &model) {
    cancel();
    m_currentMessageId = session->appendMessage(Role::Assistant).id;
    m_accumulated.clear();

    QString systemPrompt;
    QJsonArray messages;
    for (const auto &message : session->messages()) {
        if (message.id == m_currentMessageId)
            break;
        if (!message.error.isEmpty())
            continue;
        if (message.role == Role::System) {
            systemPrompt.append(message.content).append(QLatin1Char('\n'));
            continue;
        }
        QJsonArray contentParts;
        if (!message.content.isEmpty())
            contentParts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                            {QStringLiteral("text"), message.content}});
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
        if (contentParts.isEmpty())
            continue;
        messages.append(QJsonObject{
            {QStringLiteral("role"), message.role == Role::Assistant ? QStringLiteral("assistant")
                                                                     : QStringLiteral("user")},
            {QStringLiteral("content"), contentParts},
        });
    }
    systemPrompt = systemPrompt.trimmed();

    QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("max_tokens"), 4096},
        {QStringLiteral("stream"), true},
    };
    if (!systemPrompt.isEmpty())
        payload.insert(QStringLiteral("system"), systemPrompt);

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &event, const QByteArray &data) {
                const QString eventName = QString::fromUtf8(event);
                const auto object = QJsonDocument::fromJson(data).object();
                if (eventName == QStringLiteral("content_block_delta")) {
                    const QString token = object.value(QStringLiteral("delta"))
                                              .toObject()
                                              .value(QStringLiteral("text"))
                                              .toString();
                    m_accumulated.append(token);
                    emit tokenReceived(m_currentMessageId, m_accumulated);
                } else if (eventName == QStringLiteral("message_stop")) {
                    emit finished(m_currentMessageId);
                } else if (eventName == QStringLiteral("error")) {
                    emit failed(m_currentMessageId,
                                object.value(QStringLiteral("message")).toString());
                }
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

void AnthropicBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

} // namespace ocapi
