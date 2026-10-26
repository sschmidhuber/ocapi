#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace ocapi {

class SseClient : public QObject {
    Q_OBJECT

public:
    explicit SseClient(QNetworkAccessManager *nam, QObject *parent = nullptr);

    // Ollama streams newline-delimited JSON objects (one per line) instead of
    // Server-Sent Events; call setFraming() before post() for such endpoints.
    enum class Framing { Sse, Ndjson };
    void setFraming(Framing framing) { m_framing = framing; }

    void post(const QNetworkRequest &request, const QByteArray &payload);
    void cancel();

signals:
    void eventReceived(const QByteArray &event, const QByteArray &data);
    void httpError(int statusCode, const QByteArray &body);
    void networkError(const QString &error);
    void finished();

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    Framing m_framing = Framing::Sse;
};

} // namespace ocapi
