#include "backends/sttclient.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace ocapi {

namespace {

QUrl transcriptionUrl(const SttServiceConfig &config) {
    QUrl url(config.effectiveBaseUrl());
    QString basePath = url.path();
    if (!basePath.endsWith('/'))
        basePath.append('/');
    url.setPath(basePath + config.transcriptionPath().mid(1));
    return url;
}

} // namespace

SttClient::SttClient(SttServiceConfig config, QObject *parent)
    : QObject(parent), m_config(std::move(config)), m_nam(new QNetworkAccessManager(this)) {}

SttClient::~SttClient() = default;

void SttClient::start() {
    cancel();
    m_accumulated.clear();
    m_pendingRequests = 0;
    m_finishing = false;
    m_failed = false;
}

void SttClient::transcribeChunk(const QByteArray &wav, bool emitReady) {
    if (wav.isEmpty())
        return;

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"audio.wav\"")));
    filePart.setBody(wav);
    multiPart->append(filePart);

    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"model\"")));
    modelPart.setBody(m_config.effectiveModel().toUtf8());
    multiPart->append(modelPart);

    if (!m_config.language.trimmed().isEmpty()) {
        QHttpPart languagePart;
        languagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                               QVariant(QStringLiteral("form-data; name=\"language\"")));
        languagePart.setBody(m_config.language.trimmed().toUtf8());
        multiPart->append(languagePart);
    }

    QNetworkRequest request(transcriptionUrl(m_config));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("multipart/form-data; boundary=%1")
                          .arg(QString::fromLatin1(multiPart->boundary())));
    if (!m_config.apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());

    QNetworkReply *reply = m_nam->post(request, multiPart);
    multiPart->setParent(reply);

    ++m_pendingRequests;
    connect(reply, &QNetworkReply::finished, this, [this, reply, emitReady]() {
        reply->deleteLater();
        --m_pendingRequests;

        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (!m_failed) {
                m_failed = true;
                emit error(reply->errorString());
            }
        } else {
            const QJsonDocument document = QJsonDocument::fromJson(payload);
            const QString text =
                document.object().value(QStringLiteral("text")).toString().trimmed();
            onChunkDone(text, emitReady);
        }
        maybeFlush();
    });
}

void SttClient::finish() {
    m_finishing = true;
    maybeFlush();
}

void SttClient::cancel() {
    m_finishing = false;
}

void SttClient::onChunkDone(const QString &text, bool live) {
    if (text.isEmpty())
        return;
    if (!m_accumulated.isEmpty() && !m_accumulated.endsWith(' '))
        m_accumulated += ' ';
    m_accumulated += text;
    if (live && !m_finishing)
        emit intermediateTranscription(m_accumulated);
    else
        emit transcriptionUpdated(m_accumulated);
}

void SttClient::maybeFlush() {
    if (m_finishing && m_pendingRequests <= 0) {
        m_finishing = false;
        emit transcriptionUpdated(m_accumulated);
        emit finished();
    }
}

} // namespace ocapi
