#include "backends/sttclient.h"
#include "core/audiochunker.h"
#include "core/sttconfig.h"

#include <QJsonDocument>
#include <QObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace ocapi;

namespace {

// Minimal fake transcription server: accepts a multipart POST and replies
// with {"text": ...}. Records the request payload for assertions.
class FakeSttServer : public QObject {
    Q_OBJECT

public:
    explicit FakeSttServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_requests[socket].append(socket->readAll());
                respondIfComplete(socket);
            });
        });
    }

    [[nodiscard]] bool start() {
        return m_server.isListening() || m_server.listen(QHostAddress::LocalHost, 0);
    }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void setResponseText(const QString &text) { m_responseText = text; }
    [[nodiscard]] QString lastRequestBody() const { return m_lastBody; }
    [[nodiscard]] bool lastRequestHadAuthorization() const { return m_lastHadAuth; }

private:
    void respondIfComplete(QTcpSocket *socket) {
        const QByteArray &buffer = m_requests[socket];
        const auto headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QString headers = QString::fromLatin1(buffer.left(headerEnd)).toLower();
        const auto lengthIndex = headers.indexOf("content-length:");
        if (lengthIndex < 0)
            return;
        const int contentLength =
            headers.mid(lengthIndex + 15).split("\r\n").first().trimmed().toInt();
        if (buffer.size() < headerEnd + 4 + contentLength)
            return;

        m_lastBody = QString::fromUtf8(buffer.mid(headerEnd + 4, contentLength));
        // QNetworkAccessManager sends lowercased header names.
        m_lastHadAuth = headers.contains("authorization: bearer ");

        const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("text"), m_responseText}})
                                    .toJson(QJsonDocument::Compact);
        QByteArray response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: application/json\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: keep-alive\r\n";
        response += "\r\n";
        response += body;
        socket->write(response);
        m_requests[socket].clear();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QString m_responseText = QStringLiteral("hello world");
    QString m_lastBody;
    bool m_lastHadAuth = false;
};

SttServiceConfig serviceConfig(quint16 port) {
    SttServiceConfig config;
    config.name = QStringLiteral("fake");
    config.kind = SttServiceKind::WhisperApi;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.model = QStringLiteral("whisper-1");
    config.language = QStringLiteral("de");
    config.enabled = true;
    return config;
}

} // namespace

class TestSttClient : public QObject {
    Q_OBJECT

private slots:
    void transcribesChunkAndFinishes();
    void accumulatesTextAcrossChunks();
    void sendsApiKeyWhenConfigured();
    void reportsErrorOnFailure();
    void emitsNothingForEmptyChunk();

private:
    FakeSttServer m_server;
};

void TestSttClient::transcribesChunkAndFinishes() {
    QVERIFY(m_server.start());
    SttClient client(serviceConfig(m_server.port()));
    QSignalSpy updated(&client, &SttClient::transcriptionUpdated);
    QSignalSpy failed(&client, &SttClient::error);
    QSignalSpy done(&client, &SttClient::finished);

    client.start();
    client.transcribeChunk(AudioChunker::wavFromPcm(QByteArray(320, 0), 16000), false);
    client.finish();

    QVERIFY(updated.wait(5000));
    QCOMPARE(updated.first().first().toString(), QStringLiteral("hello world"));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(done.count(), 1);
}

void TestSttClient::accumulatesTextAcrossChunks() {
    QVERIFY(m_server.start());
    SttClient client(serviceConfig(m_server.port()));
    QSignalSpy updated(&client, &SttClient::transcriptionUpdated);

    client.start();
    m_server.setResponseText(QStringLiteral("first"));
    client.transcribeChunk(AudioChunker::wavFromPcm(QByteArray(320, 0), 16000), false);
    QVERIFY(updated.wait(5000));
    m_server.setResponseText(QStringLiteral("second"));
    client.transcribeChunk(AudioChunker::wavFromPcm(QByteArray(320, 0), 16000), false);
    // wait() only returns for NEW signals; the accumulated text may already
    // have arrived while processing the first response. Poll until the
    // expected text shows up.
    for (int i = 0; i < 50 && updated.last().first().toString() != QStringLiteral("first second");
         ++i) {
        updated.wait(100);
    }
    QCOMPARE(updated.last().first().toString(), QStringLiteral("first second"));
}

void TestSttClient::sendsApiKeyWhenConfigured() {
    QVERIFY(m_server.start());
    auto config = serviceConfig(m_server.port());
    config.apiKey = QStringLiteral("secret-key");
    SttClient client(config);
    QSignalSpy updated(&client, &SttClient::transcriptionUpdated);

    client.start();
    client.transcribeChunk(AudioChunker::wavFromPcm(QByteArray(320, 0), 16000), false);
    client.finish();
    QVERIFY(updated.wait(5000));

    QVERIFY(m_server.lastRequestHadAuthorization());
    QVERIFY(m_server.lastRequestBody().contains("whisper-1"));
    QVERIFY(m_server.lastRequestBody().contains("name=\"language\""));
}

void TestSttClient::reportsErrorOnFailure() {
    QVERIFY(m_server.start());
    auto config = serviceConfig(m_server.port());
    // Point at a port where no HTTP server answers.
    config.baseUrl = QStringLiteral("http://127.0.0.1:1/v1");
    SttClient client(config);
    QSignalSpy failed(&client, &SttClient::error);

    client.start();
    client.transcribeChunk(AudioChunker::wavFromPcm(QByteArray(320, 0), 16000), false);
    client.finish();
    QVERIFY(failed.wait(5000));
}

void TestSttClient::emitsNothingForEmptyChunk() {
    SttClient client(serviceConfig(1));
    QSignalSpy updated(&client, &SttClient::transcriptionUpdated);
    client.start();
    client.transcribeChunk(QByteArray(), false);
    client.finish();
    QCOMPARE(updated.count(), 1); // finish emits the empty final state
    QCOMPARE(updated.first().first().toString(), QString());
}

QTEST_MAIN(TestSttClient)
#include "tst_sttclient.moc"
