#include "backends/sttstreamclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>

namespace ocapi {

namespace {

// Streaming audio is 16-bit mono PCM at the capture rate; both providers
// support 16 kHz, so the recorder is configured for it in VoiceInput.
constexpr int kDefaultSampleRate = 16000;

} // namespace

SttStreamClient::SttStreamClient(SttServiceConfig config, QObject *parent)
    : QObject(parent), m_config(std::move(config)) {}

SttStreamClient::~SttStreamClient() {
    abort();
}

QUrl SttStreamClient::sessionUrl() const {
    QUrl url(m_config.effectiveBaseUrl());
    if (m_config.kind == SttServiceKind::OpenAiRealtime) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("intent"), QStringLiteral("transcription"));
        url.setQuery(query);
    } else if (m_config.kind == SttServiceKind::MistralRealtime) {
        QUrl base(m_config.effectiveBaseUrl());
        QString path = base.path();
        if (!path.endsWith('/'))
            path.append('/');
        base.setPath(path + QStringLiteral("v1/audio/transcriptions/realtime"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("model"), m_config.effectiveModel());
        base.setQuery(query);
        url = base;
    }
    return url;
}

QByteArray SttStreamClient::sessionUpdateJson(int sampleRate) const {
    if (m_config.kind == SttServiceKind::OpenAiRealtime) {
        QJsonObject transcription{{QStringLiteral("model"), m_config.effectiveModel()}};
        if (!m_config.language.trimmed().isEmpty())
            transcription[QStringLiteral("languages")] = QJsonArray{m_config.language.trimmed()};
        QJsonObject input{
            {QStringLiteral("format"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("audio/pcm")},
                         {QStringLiteral("rate"), sampleRate}}},
            {QStringLiteral("transcription"), transcription},
            {QStringLiteral("turn_detection"), QJsonValue()},
        };
        QJsonObject session{
            {QStringLiteral("type"), QStringLiteral("transcription")},
            {QStringLiteral("audio"), QJsonObject{{QStringLiteral("input"), input}}},
        };
        return QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("session.update")},
                                         {QStringLiteral("session"), session}})
            .toJson(QJsonDocument::Compact);
    }
    // Mistral Voxtral Realtime session.update
    QJsonObject session{
        {QStringLiteral("audio_format"),
         QJsonObject{{QStringLiteral("encoding"), QStringLiteral("pcm_s16le")},
                     {QStringLiteral("sample_rate"), sampleRate}}},
    };
    return QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("session.update")},
                                     {QStringLiteral("session"), session}})
        .toJson(QJsonDocument::Compact);
}

bool SttStreamClient::start(int sampleRate) {
    abort();
    m_sampleRate = sampleRate > 0 ? sampleRate : kDefaultSampleRate;
    m_accumulated.clear();
    m_closing = false;
    m_failed = false;

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::textMessageReceived, this,
            &SttStreamClient::onTextMessageReceived);
    connect(m_socket, &QWebSocket::connected, this, &SttStreamClient::onConnected);
    connect(m_socket, &QWebSocket::errorOccurred, this, &SttStreamClient::onErrorOccurred);
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        if (m_closing) {
            emitAccumulated();
            emit finished();
        } else if (!m_failed) {
            m_failed = true;
            emit error(tr("Connection closed unexpectedly."));
        }
    });

    QNetworkRequest request;
    request.setUrl(sessionUrl());
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());
    m_socket->open(request);
    return true;
}

void SttStreamClient::onConnected() {
    m_socket->sendTextMessage(QString::fromUtf8(sessionUpdateJson(m_sampleRate)));
}

void SttStreamClient::sendAudio(const QByteArray &pcm) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("input_audio_buffer.append")},
        {QStringLiteral("audio"), QString::fromLatin1(pcm.toBase64())},
    };
    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void SttStreamClient::stop() {
    if (!m_socket || m_closing)
        return;
    m_closing = true;
    // OpenAI: the buffer commit closes the audio turn and triggers the
    // final transcript. Mistral: input_audio.end closes the stream.
    const QString closeMessage = m_config.kind == SttServiceKind::OpenAiRealtime
                                     ? QStringLiteral("{\"type\":\"input_audio_buffer.commit\"}")
                                     : QStringLiteral("{\"type\":\"input_audio.end\"}");
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->sendTextMessage(closeMessage);
    else
        m_socket->close();
}

void SttStreamClient::abort() {
    if (!m_socket)
        return;
    m_closing = true;
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}

void SttStreamClient::onErrorOccurred() {
    if (m_failed || !m_socket)
        return;
    m_failed = true;
    emit error(m_socket->errorString());
    m_socket->abort();
}

void SttStreamClient::onTextMessageReceived(const QString &message) {
    const QJsonObject event = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = event.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("error")) {
        const QJsonObject errorObject = event.value(QStringLiteral("error")).toObject();
        if (!m_failed) {
            m_failed = true;
            emit error(errorObject.value(QStringLiteral("message")).toString(type));
        }
        return;
    }

    if (m_config.kind == SttServiceKind::OpenAiRealtime) {
        if (type == QLatin1String("conversation.item.input_audio_transcription.delta")) {
            m_accumulated += event.value(QStringLiteral("delta")).toString();
            emit intermediateTranscription(m_accumulated);
        } else if (type == QLatin1String("conversation.item.input_audio_transcription.completed")) {
            m_accumulated = event.value(QStringLiteral("transcript")).toString();
            emit transcriptionUpdated(m_accumulated);
            m_socket->close();
        }
        return;
    }

    // Mistral Voxtral Realtime events
    if (type == QLatin1String("transcription.text.delta")) {
        m_accumulated += event.value(QStringLiteral("text")).toString();
        emit intermediateTranscription(m_accumulated);
    } else if (type == QLatin1String("transcription.done")) {
        m_accumulated = event.value(QStringLiteral("text")).toString(m_accumulated);
        emit transcriptionUpdated(m_accumulated);
        m_socket->close();
    }
}

void SttStreamClient::emitAccumulated() {
    if (!m_failed)
        emit transcriptionUpdated(m_accumulated);
}

} // namespace ocapi
