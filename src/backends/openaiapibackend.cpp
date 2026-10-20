#include "backends/openaiapibackend.h"
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

QJsonValue contentForMessage(const Message &message) {
    if (message.images.isEmpty())
        return message.content;
    QJsonArray parts;
    if (!message.content.isEmpty()) {
        parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                 {QStringLiteral("text"), message.content}});
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

void OpenAiApiBackend::sendMessage(ChatSession *session, const QString &model) {
    cancel();
    m_currentMessageId = session->appendMessage(Role::Assistant).id;
    m_accumulated.clear();

    QJsonArray messages;
    for (const auto &message : session->messages()) {
        if (message.id == m_currentMessageId)
            break;
        if (!message.error.isEmpty())
            continue;
        messages.append(QJsonObject{
            {QStringLiteral("role"), message.roleName()},
            {QStringLiteral("content"), contentForMessage(message)},
        });
    }

    const QJsonObject payload{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), true},
    };

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &, const QByteArray &data) {
                const QString text = QString::fromUtf8(data);
                if (text == QStringLiteral("[DONE]"))
                    return;
                const auto object = QJsonDocument::fromJson(data).object();
                const auto choices = object.value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty())
                    return;
                const QString token = choices.first()
                                          .toObject()
                                          .value(QStringLiteral("delta"))
                                          .toObject()
                                          .value(QStringLiteral("content"))
                                          .toString();
                m_accumulated.append(token);
                emit tokenReceived(m_currentMessageId, m_accumulated);
                const QString finishReason =
                    choices.first().toObject().value(QStringLiteral("finish_reason")).toString();
                if (finishReason == QStringLiteral("stop") ||
                    finishReason == QStringLiteral("length"))
                    emit finished(m_currentMessageId);
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
    connect(m_stream.get(), &SseClient::finished, this,
            [this]() { emit finished(m_currentMessageId); });

    m_stream->post(
        makeRequest(endpoint(m_config, QStringLiteral("chat/completions")), m_config.apiKey),
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void OpenAiApiBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

} // namespace ocapi
