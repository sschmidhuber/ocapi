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

} // namespace

GeminiBackend::GeminiBackend(BackendConfig config, QObject *parent)
    : IBackend(std::move(config), parent), m_nam(new QNetworkAccessManager(this)) {}

GeminiBackend::~GeminiBackend() = default;

void GeminiBackend::listModels() {
    if (m_modelsReply) {
        m_modelsReply->disconnect(this);
        m_modelsReply->deleteLater();
    }
    QUrl url(m_config.baseUrl + QStringLiteral("/models"));
    if (!m_config.apiKey.isEmpty())
        url.setQuery(QUrlQuery({{QStringLiteral("key"), m_config.apiKey}}));
    m_modelsReply = m_nam->get(QNetworkRequest(url));
    connect(m_modelsReply, &QNetworkReply::finished, this, [this]() {
        auto *reply = m_modelsReply;
        m_modelsReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit modelsFailed(reply->errorString());
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
        emit modelsReady(names);
    });
}

void GeminiBackend::sendMessage(ChatSession *session, const QString &model) {
    cancel();
    m_currentMessageId = session->appendMessage(Role::Assistant).id;
    m_accumulated.clear();

    QString systemPrompt;
    QJsonArray contents;
    for (const auto &message : session->messages()) {
        if (message.id == m_currentMessageId)
            break;
        if (!message.error.isEmpty())
            continue;
        if (message.role == Role::System) {
            systemPrompt.append(message.content).append(QLatin1Char('\n'));
            continue;
        }
        contents.append(QJsonObject{
            {QStringLiteral("role"), geminiRole(message.role)},
            {QStringLiteral("parts"), QJsonArray{QJsonObject{
                                          {QStringLiteral("text"), message.content},
                                      }}},
        });
    }
    systemPrompt = systemPrompt.trimmed();

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

    QUrl url(m_config.baseUrl + QStringLiteral("/models/") + model +
             QStringLiteral(":streamGenerateContent?alt=sse"));
    if (!m_config.apiKey.isEmpty())
        url.setQuery(QUrlQuery({{QStringLiteral("key"), m_config.apiKey}}));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_stream = std::make_unique<SseClient>(m_nam, this);
    connect(m_stream.get(), &SseClient::eventReceived, this,
            [this](const QByteArray &, const QByteArray &data) {
                const auto candidates = QJsonDocument::fromJson(data)
                                            .object()
                                            .value(QStringLiteral("candidates"))
                                            .toArray();
                if (candidates.isEmpty())
                    return;
                const auto parts = candidates.first()
                                       .toObject()
                                       .value(QStringLiteral("content"))
                                       .toObject()
                                       .value(QStringLiteral("parts"))
                                       .toArray();
                for (const auto &part : parts) {
                    const QString token = part.toObject().value(QStringLiteral("text")).toString();
                    m_accumulated.append(token);
                }
                emit tokenReceived(m_currentMessageId, m_accumulated);
                const bool done = candidates.first()
                                      .toObject()
                                      .value(QStringLiteral("finishReason"))
                                      .toString()
                                      .isEmpty() == false;
                if (done)
                    emit finished(m_currentMessageId);
            });
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

void GeminiBackend::cancel() {
    if (m_stream)
        m_stream->cancel();
}

} // namespace ocapi
