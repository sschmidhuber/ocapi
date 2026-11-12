#include "backends/sseclient.h"
#include "backends/sseparser.h"
#include "core/debuglog.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace okapi {

namespace {

// Debug aid: when OKAPI_DEBUG_SSE is set, everything goes to stderr only —
// redirect it yourself if you want a file.
// Secrets (API keys in URLs/headers/JSON) are redacted and long payloads
// clipped, but the output still contains conversation content, so only run
// it on your own traffic.
bool sseDebugEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OKAPI_DEBUG_SSE");
    return enabled;
}

void sseLog(const QString &line) {
    if (sseDebugEnabled())
        qWarning().noquote() << "[okapi sse]" << line;
}

void sseLog(const QByteArray &line) {
    sseLog(QString::fromUtf8(line));
}

} // namespace

SseClient::SseClient(QNetworkAccessManager *nam, QObject *parent) : QObject(parent), m_nam(nam) {}

void SseClient::post(const QNetworkRequest &request, const QByteArray &payload) {
    cancel();
    m_buffer.clear();
    m_received.clear();
    sseLog(QStringLiteral("POST %1 payload %2 bytes")
               .arg(debugSanitize::redact(request.url().toString()))
               .arg(payload.size()));
    sseLog(QStringLiteral("request body: %1")
               .arg(debugSanitize::clip(debugSanitize::redact(payload), 200)));
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
    // Keep everything read so far: on error status the body must still be
    // available in onFinished() even when readyRead already consumed it.
    m_received.append(chunk);
    sseLog(QStringLiteral("read %1 bytes (buffer %2)")
               .arg(chunk.size())
               .arg(m_buffer.size() + chunk.size()));
    sseLog(QStringLiteral("chunk text: %1")
               .arg(debugSanitize::clip(debugSanitize::redact(chunk), 1000)));
    m_buffer.append(chunk);

    QList<QByteArray> events;
    if (m_framing == Framing::Ndjson)
        events = extractCompleteNdjsonRecords(m_buffer);
    else
        events = extractCompleteEvents(m_buffer);
    if (!events.isEmpty())
        sseLog(QStringLiteral("parsed %1 event(s)").arg(events.size()));

    for (const auto &rawEvent : events) {
        std::optional<SseEvent> parsed;
        if (m_framing == Framing::Ndjson) {
            if (!rawEvent.trimmed().isEmpty())
                parsed = SseEvent{.event = "message", .data = rawEvent.trimmed()};
        } else {
            parsed = parseSseEventBlock(rawEvent);
        }
        if (parsed)
            sseLog(QStringLiteral("event=%1 data=%2")
                       .arg(QString::fromUtf8(parsed->event),
                            debugSanitize::clip(debugSanitize::redact(parsed->data), 1000)));
        else
            sseLog(QStringLiteral("raw event dropped (no data): %1")
                       .arg(debugSanitize::clip(debugSanitize::redact(rawEvent), 400)));
        if (parsed)
            emit eventReceived(parsed->event, parsed->data);
    }
    sseLog(QStringLiteral("buffer after extract: %1 bytes").arg(m_buffer.size()));
}

void SseClient::onFinished() {
    if (!m_reply)
        return;

    // Flush any trailing event without a blank-line terminator.
    if (!m_buffer.trimmed().isEmpty())
        onReadyRead();

    const QNetworkReply::NetworkError error = m_reply->error();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    sseLog(QStringLiteral("finished status=%1 content-type=%2 received=%3 bytes error=%4")
               .arg(status)
               .arg(m_reply->header(QNetworkRequest::ContentTypeHeader).toString())
               .arg(m_received.size())
               .arg(m_reply->errorString()));
    if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
        if (status >= 400)
            emit httpError(status, m_received);
        else
            emit networkError(m_reply->errorString());
    }

    m_reply->deleteLater();
    m_reply = nullptr;
    emit finished();
}

} // namespace okapi
