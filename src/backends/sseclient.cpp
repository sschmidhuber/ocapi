#include "backends/sseclient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ocapi {

SseClient::SseClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

void SseClient::post(const QNetworkRequest& request, const QByteArray& payload)
{
    cancel();
    m_buffer.clear();
    m_reply = m_nam->post(request, payload);
    connect(m_reply, &QNetworkReply::readyRead, this, &SseClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &SseClient::onFinished);
}

void SseClient::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void SseClient::onReadyRead()
{
    if (!m_reply)
        return;
    m_buffer.append(m_reply->readAll());

    // SSE events are separated by a blank line ("\n\n" or "\r\n\r\n").
    int pos = 0;
    while (true) {
        int idx = m_buffer.indexOf("\n\n", pos);
        int sepLen = 2;
        if (idx < 0) {
            idx = m_buffer.indexOf("\r\n\r\n", pos);
            sepLen = 4;
        }
        if (idx < 0)
            break;

        const QByteArray rawEvent = m_buffer.mid(pos, idx - pos);
        m_buffer.remove(0, idx + sepLen - pos);
        pos = 0;

        QByteArray event = QStringLiteral("message").toUtf8();
        QByteArray data;
        const auto lines = rawEvent.split('\n');
        for (const auto& line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith(':'))
                continue; // comment / heartbeat
            const int colon = trimmed.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray field = trimmed.left(colon);
            QByteArray value = trimmed.mid(colon + 1);
            if (value.startsWith(' '))
                value.remove(0, 1);
            if (field == "event")
                event = value;
            else if (field == "data")
                data.append(value).append('\n');
        }
        if (!data.isEmpty() && data.endsWith('\n'))
            data.chop(1);
        if (rawEvent.trimmed().isEmpty())
            continue;
        emit eventReceived(event, data);
    }
}

void SseClient::onFinished()
{
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
