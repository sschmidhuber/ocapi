#include "backends/anthropicbackend.h"
#include "backends/sseclient.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

namespace ocapi {

namespace {

QUrl endpoint(const BackendConfig &config, const QString &path) {
    QUrl url(config.baseUrl);
    QString basePath = url.path();
    if (!basePath.endsWith('/'))
        basePath.append('/');
    url.setPath(basePath + path);
    return url;
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
    // Anthropic has no public model-listing endpoint; offer the well-known models.
    emit modelsReady({
        QStringLiteral("claude-sonnet-4-5"),
        QStringLiteral("claude-opus-4-1"),
        QStringLiteral("claude-haiku-4-5"),
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
        messages.append(QJsonObject{
            {QStringLiteral("role"), message.role == Role::Assistant ? QStringLiteral("assistant")
                                                                     : QStringLiteral("user")},
            {QStringLiteral("content"), message.content},
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
