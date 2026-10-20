#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace ocapi {

class SseClient : public QObject
{
    Q_OBJECT

public:
    explicit SseClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    void post(const QNetworkRequest& request, const QByteArray& payload);
    void cancel();

signals:
    void eventReceived(const QByteArray& event, const QByteArray& data);
    void httpError(int statusCode, const QByteArray& body);
    void networkError(const QString& error);
    void finished();

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager* m_nam;
    QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;
};

} // namespace ocapi
