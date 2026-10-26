#include "backends/sseclient.h"
#include "backends/sseparser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace ocapi {

namespace {

// Debug aid: when OCAPI_DEBUG_SSE is set, every raw chunk read from the
// network and every parsed event are appended to a log file so stream
// parsing problems can be diagnosed from a real conversation. The file is
// written into the app data location as sse-debug-<timestamp>.log.
bool sseDebugEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OCAPI_DEBUG_SSE");
    return enabled;
}

QFile &sseDebugFile() {
    static QFile file;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        const QString name =
            QStringLiteral("sse-debug-%1.log")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        file.setFileName(dir + QLatin1Char('/') + name);
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    return file;
}

void sseDebug(const QByteArray &line) {
    if (!sseDebugEnabled())
        return;
    QFile &file = sseDebugFile();
    if (file.isOpen())
        file.write(line);
}

QByteArray hexDump(const QByteArray &data) {
    return data.toHex(' ');
}

} // namespace

SseClient::SseClient(QNetworkAccessManager *nam, QObject *parent) : QObject(parent), m_nam(nam) {}

void SseClient::post(const QNetworkRequest &request, const QByteArray &payload) {
    cancel();
    m_buffer.clear();
    if (sseDebugEnabled()) {
        sseDebug("=== POST " + request.url().toEncoded() + "\n");
        sseDebug("=== request body: " + payload + "\n");
    }
    m_reply = m_nam->post(request, payload);
    connect(m_reply, &QNetworkReply::readyRead, this, &SseClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &SseClient::onFinished);
}

void SseClient::cancel() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void SseClient::onReadyRead() {
    if (!m_reply)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (sseDebugEnabled())
        sseDebug("--- read " + QByteArray::number(chunk.size()) +
                 " bytes\n--- hex: " + hexDump(chunk) + "\n--- text: " + chunk + "\n");
    m_buffer.append(chunk);

    QList<QByteArray> events;
    if (m_framing == Framing::Ndjson)
        events = extractCompleteNdjsonRecords(m_buffer);
    else
        events = extractCompleteEvents(m_buffer);

    for (const auto &rawEvent : events) {
        std::optional<SseEvent> parsed;
        if (m_framing == Framing::Ndjson) {
            if (!rawEvent.trimmed().isEmpty())
                parsed = SseEvent{"message", rawEvent.trimmed()};
        } else {
            parsed = parseSseEventBlock(rawEvent);
        }
        if (sseDebugEnabled()) {
            sseDebug(">>> raw event hex: " + hexDump(rawEvent) +
                     "\n>>> raw event text: " + rawEvent + "\n");
            if (parsed)
                sseDebug(">>> parsed event=" + parsed->event + " data=" + parsed->data + "\n");
            else
                sseDebug(">>> parsed: NO DATA (event dropped)\n");
        }
        if (parsed)
            emit eventReceived(parsed->event, parsed->data);
    }
    if (sseDebugEnabled())
        sseDebug("--- buffer after extract (" + QByteArray::number(m_buffer.size()) +
                 " bytes), hex: " + hexDump(m_buffer) + "\n");
}

void SseClient::onFinished() {
    if (!m_reply)
        return;

    // Flush any trailing event without a blank-line terminator.
    if (!m_buffer.trimmed().isEmpty())
        onReadyRead();

    const QNetworkReply::NetworkError error = m_reply->error();
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400)
            emit httpError(status, m_reply->readAll());
        else
            emit networkError(m_reply->errorString());
    }

    m_reply->deleteLater();
    m_reply = nullptr;
    emit finished();
}

} // namespace ocapi
