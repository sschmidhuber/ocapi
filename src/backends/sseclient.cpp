#include "backends/sseclient.h"
#include "backends/sseparser.h"

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

    for (const auto& rawEvent : extractCompleteEvents(m_buffer)) {
        const auto parsed = parseSseEventBlock(rawEvent);
        if (parsed)
            emit eventReceived(parsed->event, parsed->data);
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
