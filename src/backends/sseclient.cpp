#include "backends/sseclient.h"
#include "backends/sseparser.h"
#include "core/debuglog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace okapi {

namespace {

// Debug aid: when OKAPI_DEBUG_SSE is set, raw chunks read from the network
// and parsed events are appended to a log file so stream parsing problems
// can be diagnosed from a real conversation. The file is written into the
// app data location as sse-debug-<timestamp>.log. Secrets (API keys in
// URLs/headers/JSON) are redacted and entries are truncated, but the log
// still contains conversation content and is capped at kMaxLogBytes, so
// only run it on your own traffic and do not share the file blindly.
constexpr qint64 kMaxLogBytes = 5LL * 1024 * 1024;

bool sseDebugEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OKAPI_DEBUG_SSE");
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
    if (!file.isOpen())
        return;
    if (file.size() >= kMaxLogBytes) {
        static bool capNotified = false;
        if (!capNotified) {
            capNotified = true;
            file.write("\n--- log size cap reached, further entries skipped ---\n");
        }
        return;
    }
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
    m_received.clear();
    if (sseDebugEnabled()) {
        sseDebug("=== POST " + debugSanitize::redact(request.url().toEncoded()) + "\n");
        sseDebug("=== request body: " + debugSanitize::clip(debugSanitize::redact(payload), 200) +
                 "\n");
        // Also to stderr, so the terminal shows the request when the app is
        // started from a console (the file log above is easy to miss).
        qWarning().noquote() << "[okapi sse] POST"
                             << debugSanitize::redact(request.url().toString()) << "payload"
                             << payload.size() << "bytes";
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
    // Keep everything read so far: on error status the body must still be
    // available in onFinished() even when readyRead already consumed it.
    m_received.append(chunk);
    if (sseDebugEnabled())
        qWarning().noquote() << "[okapi sse] read" << chunk.size() << "bytes (buffer now"
                             << m_buffer.size() + chunk.size() << ")";
    if (sseDebugEnabled())
        sseDebug("--- read " + QByteArray::number(chunk.size()) +
                 " bytes\n--- hex: " + debugSanitize::clip(hexDump(chunk), 400) +
                 "\n--- text: " + debugSanitize::clip(debugSanitize::redact(chunk), 1000) + "\n");
    m_buffer.append(chunk);

    QList<QByteArray> events;
    if (m_framing == Framing::Ndjson)
        events = extractCompleteNdjsonRecords(m_buffer);
    else
        events = extractCompleteEvents(m_buffer);
    if (sseDebugEnabled() && !events.isEmpty())
        qWarning().noquote() << "[okapi sse] parsed" << events.size() << "event(s)";

    for (const auto &rawEvent : events) {
        std::optional<SseEvent> parsed;
        if (m_framing == Framing::Ndjson) {
            if (!rawEvent.trimmed().isEmpty())
                parsed = SseEvent{.event = "message", .data = rawEvent.trimmed()};
        } else {
            parsed = parseSseEventBlock(rawEvent);
        }
        if (sseDebugEnabled()) {
            sseDebug(">>> raw event hex: " + debugSanitize::clip(hexDump(rawEvent), 400) +
                     "\n>>> raw event text: " +
                     debugSanitize::clip(debugSanitize::redact(rawEvent), 1000) + "\n");
            if (parsed)
                sseDebug(">>> parsed event=" + parsed->event + " data=" +
                         debugSanitize::clip(debugSanitize::redact(parsed->data), 1000) + "\n");
            else
                sseDebug(">>> parsed: NO DATA (event dropped)\n");
        }
        if (parsed)
            emit eventReceived(parsed->event, parsed->data);
    }
    if (sseDebugEnabled())
        sseDebug("--- buffer after extract (" + QByteArray::number(m_buffer.size()) +
                 " bytes), hex: " + debugSanitize::clip(hexDump(m_buffer), 400) + "\n");
}

void SseClient::onFinished() {
    if (!m_reply)
        return;

    // Flush any trailing event without a blank-line terminator.
    if (!m_buffer.trimmed().isEmpty())
        onReadyRead();

    const QNetworkReply::NetworkError error = m_reply->error();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (sseDebugEnabled())
        qWarning().noquote() << "[okapi sse] finished status=" << status << "content-type="
                             << m_reply->header(QNetworkRequest::ContentTypeHeader).toString()
                             << "received=" << m_received.size() << "bytes"
                             << "error=" << m_reply->errorString();
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
