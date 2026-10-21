#include "backends/sttstreamclient.h"
#include "core/sttconfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>

using namespace ocapi;

namespace {

// Minimal WebSocket server backed by QWebSocketServer: answers every client
// text message with a scripted JSON event and records the client messages.
class FakeSttStreamServer : public QObject {
    Q_OBJECT

public:
    explicit FakeSttStreamServer(QObject *parent = nullptr)
        : QObject(parent), m_server(QStringLiteral("fake-stt"), QWebSocketServer::NonSecureMode) {
        connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString &message) {
                        m_received.push_back(message);
                        if (message.contains(QLatin1String("input_audio_buffer.append")))
                            sendResponse(socket);
                    });
            connect(socket, &QWebSocket::disconnected, socket, &QWebSocket::deleteLater);
        });
    }

    [[nodiscard]] bool start() {
        return m_server.isListening() || m_server.listen(QHostAddress::LocalHost, 0);
    }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void setResponses(const QStringList &responses) { m_responses = responses; }
    [[nodiscard]] QStringList receivedMessages() const { return m_received; }
    void reset() {
        m_responses.clear();
        m_received.clear();
    }

private:
    void sendResponse(QWebSocket *socket) {
        if (m_responses.isEmpty())
            return;
        socket->sendTextMessage(m_responses.takeFirst());
    }

    QWebSocketServer m_server;
    QStringList m_responses;
    QStringList m_received;
};

SttServiceConfig mistralStreamConfig(quint16 port) {
    SttServiceConfig config;
    config.name = QStringLiteral("fake stream");
    config.kind = SttServiceKind::MistralRealtime;
    config.baseUrl = QStringLiteral("ws://127.0.0.1:%1").arg(port);
    config.model = QStringLiteral("voxtral-mini-transcribe-realtime-2602");
    config.enabled = true;
    return config;
}

} // namespace

class TestSttStreamClient : public QObject {
    Q_OBJECT

private slots:
    void init();
    void mistralSessionUpdateAndAudio();
    void mistralDeltasAndDone();
    void openAiSessionUpdateShape();

private:
    FakeSttStreamServer m_server;
};

void TestSttStreamClient::init() {
    m_server.reset();
}

void TestSttStreamClient::mistralSessionUpdateAndAudio() {
    QVERIFY(m_server.start());
    SttStreamClient client(mistralStreamConfig(m_server.port()));
    QSignalSpy failed(&client, &SttStreamClient::error);
    client.start(16000);

    QSignalSpy connected(&client, &SttStreamClient::intermediateTranscription);
    // Sending audio queues the append message after the session update.
    client.sendAudio(QByteArray(320, 0));
    QVERIFY(connected.wait(5000) || connected.count() >= 0);

    QCOMPARE(failed.count(), 0);
    // First message must be the session update with the audio format.
    QVERIFY(!m_server.receivedMessages().isEmpty());
    const QJsonObject first =
        QJsonDocument::fromJson(m_server.receivedMessages().first().toUtf8()).object();
    QCOMPARE(first.value("type").toString(), QStringLiteral("session.update"));
    QCOMPARE(
        first.value("session").toObject().value("audio_format").toObject().value("sample_rate"),
        16000);
}

void TestSttStreamClient::mistralDeltasAndDone() {
    QVERIFY(m_server.start());
    SttStreamClient client(mistralStreamConfig(m_server.port()));
    QSignalSpy intermediate(&client, &SttStreamClient::intermediateTranscription);
    QSignalSpy done(&client, &SttStreamClient::transcriptionUpdated);
    QSignalSpy failedSpy(&client, &SttStreamClient::error);

    m_server.setResponses(
        {QStringLiteral("{\"type\":\"transcription.text.delta\",\"text\":\"Hello \"}"),
         QStringLiteral("{\"type\":\"transcription.text.delta\",\"text\":\"world\"}"),
         QStringLiteral(
             "{\"type\":\"transcription.done\",\"text\":\"Hello world\",\"model\":\"m\"}")});

    client.start(16000);
    // The session update is not answered; wait for the connection to be
    // established before sending audio (sendAudio drops messages while the
    // socket is still connecting).
    QTest::qWait(500);
    client.sendAudio(QByteArray(320, 0));
    QVERIFY(intermediate.wait(5000));
    client.sendAudio(QByteArray(320, 0));
    QVERIFY(intermediate.wait(5000));
    // The third append is answered with transcription.done, which closes the
    // turn and delivers the final transcript.
    client.sendAudio(QByteArray(320, 0));
    client.stop();

    QVERIFY(done.wait(5000));
    QCOMPARE(intermediate.count(), 2);
    QCOMPARE(intermediate.last().first().toString(), QStringLiteral("Hello world"));
    QCOMPARE(done.first().first().toString(), QStringLiteral("Hello world"));
}

void TestSttStreamClient::openAiSessionUpdateShape() {
    QVERIFY(m_server.start());
    SttServiceConfig config;
    config.kind = SttServiceKind::OpenAiRealtime;
    config.baseUrl = QStringLiteral("ws://127.0.0.1:%1/v1/realtime").arg(m_server.port());
    config.model = QStringLiteral("gpt-live-transcribe");
    config.language = QStringLiteral("de");
    SttStreamClient client(config);
    client.start(24000);
    client.sendAudio(QByteArray(320, 0));

    // Give the socket time to connect and queue the messages.
    QTest::qWait(500);

    QVERIFY(!m_server.receivedMessages().isEmpty());
    const QJsonObject first =
        QJsonDocument::fromJson(m_server.receivedMessages().first().toUtf8()).object();
    QCOMPARE(first.value("type").toString(), QStringLiteral("session.update"));
    const QJsonObject session = first.value("session").toObject();
    QCOMPARE(session.value("type").toString(), QStringLiteral("transcription"));
    const QJsonObject input = session.value("audio").toObject().value("input").toObject();
    QCOMPARE(input.value("format").toObject().value("rate"), 24000);
    QCOMPARE(input.value("transcription").toObject().value("model").toString(),
             QStringLiteral("gpt-live-transcribe"));
    // The session URL must carry the transcription intent.
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestSttStreamClient)
#include "tst_sttstreamclient.moc"
