#include "core/audiochunker.h"
#include "core/audioformat.h"
#include "core/ttsconfig.h"
#include "core/ttsplayer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace ocapi;

namespace {

// Minimal fake TTS server on loopback: accepts POST /audio/speech and
// replies with WAV bytes (so TtsPlayer takes its direct PCM path, no MP3
// decoder needed in tests). The audio payload is configurable; when
// echoInput is set, the PCM data is the request's "input" text, so tests
// can attribute each response to its request.
class FakeTtsServer : public QObject {
    Q_OBJECT

public:
    explicit FakeTtsServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                m_requests[socket].append(socket->readAll());
                respondIfComplete(socket);
            });
        });
    }

    [[nodiscard]] bool start() {
        return m_server.isListening() || m_server.listen(QHostAddress::LocalHost, 0);
    }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    void setErrorResponse(int status, const QByteArray &body) {
        m_errorStatus = status;
        m_errorBody = body;
    }
    void setEchoInput(bool on) { m_echoInput = on; }
    void resetCounters() { m_bodies.clear(); }
    [[nodiscard]] int requestCount() const { return static_cast<int>(m_bodies.size()); }
    [[nodiscard]] QStringList requestBodies() const { return m_bodies; }

private:
    void respondIfComplete(QTcpSocket *socket) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
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

        const QString requestBody = QString::fromUtf8(buffer.mid(headerEnd + 4, contentLength));
        m_bodies.push_back(requestBody);
        QByteArray audio = AudioChunker::wavFromPcm(QByteArray(320, 0), 16000);
        if (m_echoInput) {
            const QJsonDocument doc = QJsonDocument::fromJson(requestBody.toUtf8());
            audio = AudioChunker::wavFromPcm(
                doc.object().value(QStringLiteral("input")).toString().toUtf8(), 16000);
        }
        QByteArray response;
        if (m_errorStatus > 0) {
            response = "HTTP/1.1 " + QByteArray::number(m_errorStatus) + " Error\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(m_errorBody.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += m_errorBody;
        } else {
            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: audio/wav\r\n";
            response += "Content-Length: " + QByteArray::number(audio.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += audio;
        }
        socket->write(response);
        socket->disconnectFromHost();
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        m_requests[socket].clear();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requests;
    int m_errorStatus = 0;
    QByteArray m_errorBody;
    bool m_echoInput = false;
    QStringList m_bodies;
};

TtsServiceConfig serviceConfig(quint16 port) {
    TtsServiceConfig config;
    config.name = QStringLiteral("fake");
    config.kind = TtsServiceKind::OpenAiTts;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.model = QStringLiteral("tts-1");
    config.voice = QStringLiteral("alloy");
    config.enabled = true;
    return config;
}

} // namespace

class TestTtsPlayer : public QObject {
    Q_OBJECT

private slots:
    void init() { m_server.resetCounters(); }

    void synthesizesSentencesWhileStreaming();
    void synthesizesRemainderOnEndOfText();
    void skipsSynthesisWhenNoServiceConfigured();
    void leavesSpeakingStateOnServiceError();
    void restartAfterStopRefetchesFromScratch();

private:
    FakeTtsServer m_server;
};

void TestTtsPlayer::synthesizesSentencesWhileStreaming() {
    QVERIFY(m_server.start());
    m_server.setEchoInput(true);
    TtsPlayer player;
    player.setService(serviceConfig(m_server.port()));
    QSignalSpy speakingSpy(&player, &TtsPlayer::speakingChanged);

    // Simulate a streaming assistant message: the accumulated text grows
    // token by token. Only complete sentences may be synthesized.
    player.feedText(QStringLiteral("Hello world. How are"));
    QTest::qWait(300); // let the first request round-trip
    player.feedText(QStringLiteral("Hello world. How are you today?"));
    player.endOfText();

    // Both sentences were sent (the second one flushed at endOfText).
    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 2, 5000);
    QVERIFY(m_server.requestBodies().first().contains(QStringLiteral("Hello world.")));
    QVERIFY(!m_server.requestBodies().first().contains(QStringLiteral("How are")));
    QVERIFY(m_server.requestBodies().at(1).contains(QStringLiteral("How are you today?")));

    // The player reported the speaking state at least once.
    QVERIFY(speakingSpy.count() >= 1);
    QVERIFY(speakingSpy.first().first().toBool());
}

void TestTtsPlayer::synthesizesRemainderOnEndOfText() {
    QVERIFY(m_server.start());
    m_server.setEchoInput(true);
    TtsPlayer player;
    player.setService(serviceConfig(m_server.port()));

    // No sentence terminator: nothing is synthesized while streaming...
    player.feedText(QStringLiteral("This has no terminator"));
    QTest::qWait(200);
    QCOMPARE(m_server.requestCount(), 0);
    // ...until endOfText flushes the remainder.
    player.endOfText();
    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 1, 5000);
    QVERIFY(m_server.requestBodies().first().contains(QStringLiteral("This has no terminator")));
}

void TestTtsPlayer::skipsSynthesisWhenNoServiceConfigured() {
    TtsPlayer player; // no setService()
    QSignalSpy speakingSpy(&player, &TtsPlayer::speakingChanged);
    QSignalSpy errorSpy(&player, &TtsPlayer::error);

    player.feedText(QStringLiteral("Hello world."));
    player.endOfText();
    QTest::qWait(100);

    QCOMPARE(speakingSpy.count(), 0); // never entered the speaking state
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(!player.isSpeaking());
}

void TestTtsPlayer::leavesSpeakingStateOnServiceError() {
    QVERIFY(m_server.start());
    m_server.setErrorResponse(500, QByteArrayLiteral("{\"error\":\"boom\"}"));
    TtsPlayer player;
    player.setService(serviceConfig(m_server.port()));
    QSignalSpy speakingSpy(&player, &TtsPlayer::speakingChanged);
    QSignalSpy errorSpy(&player, &TtsPlayer::error);

    player.feedText(QStringLiteral("Hello world."));
    player.endOfText();

    // The request fails: an error is reported and the speaking state ends
    // (instead of the player being stuck in "speaking" forever).
    QTRY_VERIFY_WITH_TIMEOUT(!errorSpy.isEmpty(), 5000);
    QVERIFY(errorSpy.first().first().toString().contains(QStringLiteral("500")));
    QTRY_VERIFY_WITH_TIMEOUT(!player.isSpeaking(), 5000);
    // speakingChanged went true (request enqueued) and back to false.
    QVERIFY(speakingSpy.count() >= 2);
    QCOMPARE(speakingSpy.last().first().toBool(), false);
}

void TestTtsPlayer::restartAfterStopRefetchesFromScratch() {
    QVERIFY(m_server.start());
    m_server.setEchoInput(true);
    TtsPlayer player;
    player.setService(serviceConfig(m_server.port()));

    // "First sentence." is a complete sentence: it is synthesized right
    // away, before stop() arrives.
    player.feedText(QStringLiteral("First sentence"));
    QTest::qWait(150); // no terminator yet: nothing sent
    QCOMPARE(m_server.requestCount(), 0);
    player.stop(); // aborts before the sentence was ever synthesized
    player.feedText(QStringLiteral("Second sentence."));
    player.endOfText();

    QTRY_COMPARE_WITH_TIMEOUT(m_server.requestCount(), 1, 5000);
    // After stop(), the restarted message is re-split from scratch: the
    // first (stopped) sentence is not re-sent.
    QVERIFY(m_server.requestBodies().first().contains(QStringLiteral("Second sentence.")));
    QVERIFY(!m_server.requestBodies().first().contains(QStringLiteral("First sentence")));
}

QTEST_GUILESS_MAIN(TestTtsPlayer)

#include "tst_ttsplayer.moc"
