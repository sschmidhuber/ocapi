#include "backends/ttsclient.h"

#include "core/audioformat.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace ocapi {

namespace {

constexpr int kMaxInFlight = 2; // overlap synthesis with playback

QUrl speechUrl(const TtsServiceConfig &config) {
    QUrl url(config.effectiveBaseUrl());
    QString basePath = url.path();
    if (!basePath.endsWith('/'))
        basePath.append('/');
    const QString path = config.speechPath();
    url.setPath(basePath + (path.startsWith('/') ? path.mid(1) : path));
    return url;
}

void applyCommonHeaders(QNetworkRequest &request, const TtsServiceConfig &config) {
    if (!config.apiKey.isEmpty()) {
        if (config.kind == TtsServiceKind::ElevenLabs)
            request.setRawHeader("xi-api-key", config.apiKey.toUtf8());
        else
            request.setRawHeader("Authorization", QByteArray("Bearer ") + config.apiKey.toUtf8());
    }
}

} // namespace

TtsClient::TtsClient(TtsServiceConfig config, QObject *parent)
    : QObject(parent), m_config(std::move(config)), m_nam(new QNetworkAccessManager(this)) {}

TtsClient::~TtsClient() = default;

void TtsClient::synthesize(const QString &text) {
    if (text.trimmed().isEmpty())
        return;
    m_queue.push_back({text, m_nextSequence++});

    sendNext();
}

void TtsClient::cancel() {
    m_cancelled = true;
    m_queue.clear();
    m_inFlight.clear();
    m_completed.clear();
}

void TtsClient::sendNext() {
    if (m_cancelled)
        return;
    while (!m_queue.isEmpty() && m_inFlight.size() < kMaxInFlight) {
        const PendingRequest request = m_queue.takeFirst();
        m_inFlight.push_back(request);

        QNetworkReply *reply = nullptr;
        if (m_config.kind == TtsServiceKind::Piper) {
            // Piper HTTP API: GET /api/tts?text=...&speaker=...
            QUrl url = speechUrl(m_config);
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("text"), request.text);
            if (!m_config.effectiveModel().isEmpty())
                query.addQueryItem(QStringLiteral("speaker"), m_config.effectiveModel());
            url.setQuery(query);
            QNetworkRequest httpRequest(url);
            applyCommonHeaders(httpRequest, m_config);
            reply = m_nam->get(httpRequest);
        } else {
            QNetworkRequest httpRequest(speechUrl(m_config));
            httpRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                                  QStringLiteral("application/json"));
            applyCommonHeaders(httpRequest, m_config);
            QJsonObject payload{
                {QStringLiteral("model"), m_config.effectiveModel()},
                {QStringLiteral("input"), request.text},
                {QStringLiteral("voice"), m_config.effectiveVoice()},
                {QStringLiteral("response_format"),
                 TtsServiceConfig::responseFormat(m_config.kind)},
            };
            if (m_config.kind == TtsServiceKind::ElevenLabs) {
                // ElevenLabs expects "text" and "model_id" instead of
                // "input" and "model".
                payload.remove(QStringLiteral("input"));
                payload.remove(QStringLiteral("model"));
                payload.insert(QStringLiteral("text"), request.text);
                payload.insert(QStringLiteral("model_id"), m_config.effectiveModel());
            }
            if (!m_config.language.trimmed().isEmpty())
                payload.insert(QStringLiteral("language"), m_config.language.trimmed());
            reply = m_nam->post(httpRequest, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        }

        const int sequence = request.sequence;
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, sequence]() { onReplyFinished(reply, sequence); });
    }
}

void TtsClient::onReplyFinished(QNetworkReply *reply, int sequence) {
    reply->deleteLater();
    for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ++it) {
        if (it->sequence == sequence) {
            m_inFlight.erase(it);
            break;
        }
    }
    if (m_cancelled)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        emit error(reply->errorString());
        // Keep sequence numbers gapless for ordering: treat failures as empty.
        m_completed.insert(sequence, {QByteArray(), QString()});
    } else {
        const QByteArray audio = reply->readAll();
        const auto kind = AudioFormat::detect(audio);
        if (audio.isEmpty() || kind == AudioFormat::Kind::Unknown) {
            emit error(tr("The TTS service returned no decodable audio."));
            m_completed.insert(sequence, {QByteArray(), QString()});
        } else {
            m_completed.insert(sequence, {audio, AudioFormat::mimeType(kind)});
        }
    }
    flushCompleted();
    sendNext();
}

void TtsClient::flushCompleted() {
    // Emit results strictly in enqueue order; skip failed (empty) entries
    // without emitting audioReady for them.
    while (m_completed.contains(m_nextPlaySequence)) {
        const auto result = m_completed.take(m_nextPlaySequence);
        ++m_nextPlaySequence;
        if (!result.first.isEmpty())
            emit audioReady(result.first, result.second);
    }
}

} // namespace ocapi
