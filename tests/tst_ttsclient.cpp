#include "backends/ttsclient.h"
#include "core/audiochunker.h"
#include "core/audioformat.h"
#include "core/ttsconfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace ocapi;

namespace {

// Minimal fake TTS server: accepts a POST /audio/speech and replies with
// WAV bytes (so no real MP3 encoder is needed). The response delay is
// configurable per request so out-of-order replies can be simulated.
// Records request payloads for assertions.
class FakeTtsServer : public QObject {
    Q_OBJECT

public:
    explicit FakeTtsServer(QObject *parent = nullptr) : QObject(parent) {
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

    void setDelayMs(int ms) { m_delayMs = ms; }
    void setAudio(const QByteArray &audio) { m_audio = audio; }
    // When set, the response body is WAV whose PCM data is the request's
    // "input" text, so each response can be attributed to its request.
    void setEchoInput(bool on) { m_echoInput = on; }
    // When set, the server answers with this HTTP status and body instead
    // of audio, to simulate a service error.
    void setErrorResponse(int status, const QByteArray &body) {
        m_errorStatus = status;
        m_errorBody = body;
    }
    // When set, the server wraps the audio in a JSON {"audio_data":
    // "<base64>"} envelope, like Mistral's speech endpoint does without an
    // explicit response_format.
    void setJsonEnvelope(bool on) { m_jsonEnvelope = on; }
    // Responses whose body contains \p marker are delayed by \p ms, so
    // out-of-order replies can be simulated.
    void setSlowMarker(const QString &marker, int ms) {
        m_slowMarker = marker;
        m_slowMarkerMs = ms;
    }
    [[nodiscard]] QStringList requestBodies() const { return m_bodies; }
    [[nodiscard]] QStringList authorizationHeaders() const { return m_authHeaders; }
    [[nodiscard]] QStringList xiApiKeyHeaders() const { return m_xiApiKeys; }
    [[nodiscard]] int requestCount() const { return static_cast<int>(m_bodies.size()); }

    void resetCounters() {
        m_bodies.clear();
        m_authHeaders.clear();
        m_xiApiKeys.clear();
        m_delayMs = 0;
        m_echoInput = false;
        m_slowMarker.clear();
        m_slowMarkerMs = 0;
        m_errorStatus = 0;
        m_errorBody.clear();
        m_jsonEnvelope = false;
        m_audio = AudioChunker::wavFromPcm(QByteArray(320, 0), 16000);
    }

private:
    void respondIfComplete(QTcpSocket *socket) {
        const QByteArray &buffer = m_requests[socket];
        const auto headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QString headers = QString::fromLatin1(buffer.left(headerEnd));
        const QString lowerHeaders = headers.toLower();
        const auto lengthIndex = lowerHeaders.indexOf("content-length:");
        if (lengthIndex < 0)
            return;
        const int contentLength =
            lowerHeaders.mid(lengthIndex + 15).split("\r\n").first().trimmed().toInt();
        if (buffer.size() < headerEnd + 4 + contentLength)
            return;

        m_bodies.push_back(QString::fromUtf8(buffer.mid(headerEnd + 4, contentLength)));
        // QNetworkAccessManager sends header names in lowercase.
        const QRegularExpression authHeader(QStringLiteral("\\r\\nauthorization: ([^\\r\\n]*)"),
                                            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression xiKeyHeader(QStringLiteral("\\r\\nxi-api-key: ([^\\r\\n]*)"),
                                             QRegularExpression::CaseInsensitiveOption);
        const auto authMatch = authHeader.match(headers);
        m_authHeaders.push_back(authMatch.hasMatch() ? authMatch.captured(1) : QString());
        const auto xiKeyMatch = xiKeyHeader.match(headers);
        m_xiApiKeys.push_back(xiKeyMatch.hasMatch() ? xiKeyMatch.captured(1) : QString());

        const QString requestBody = QString::fromUtf8(buffer.mid(headerEnd + 4, contentLength));
        QByteArray audio = m_audio;
        if (m_echoInput) {
            const QJsonDocument doc = QJsonDocument::fromJson(requestBody.toUtf8());
            audio = AudioChunker::wavFromPcm(
                doc.object().value(QStringLiteral("input")).toString().toUtf8(), 16000);
        }
        int delayMs = m_delayMs;
        if (!m_slowMarker.isEmpty() && requestBody.contains(m_slowMarker))
            delayMs = m_slowMarkerMs;
        QByteArray response;
        if (m_errorStatus > 0) {
            response = "HTTP/1.1 " + QByteArray::number(m_errorStatus) + " Error\r\n";
            response += "Content-Type: application/json\r\n";
            response += "Content-Length: " + QByteArray::number(m_errorBody.size()) + "\r\n";
            response += "Connection: keep-alive\r\n";
            response += "\r\n";
            response += m_errorBody;
        } else {
            QByteArray body = audio;
            QByteArray contentType = "audio/wav";
            if (m_jsonEnvelope) {
                const QJsonObject envelope{
                    {QStringLiteral("audio_data"), QString::fromLatin1(audio.toBase64())}};
                body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
                contentType = "application/json";
            }
            response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: " + contentType + "\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: keep-alive\r\n";
            response += "\r\n";
            response += body;
        }
        auto *pending = new QTimer(socket);
        pending->setSingleShot(true);
        connect(pending, &QTimer::timeout, socket, [socket, response, pending]() {
            socket->write(response);
            pending->deleteLater();
        });
        pending->start(delayMs);
        m_requests[socket].clear();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QByteArray m_audio = AudioChunker::wavFromPcm(QByteArray(320, 0), 16000);
    int m_delayMs = 0;
    bool m_echoInput = false;
    QString m_slowMarker;
    int m_slowMarkerMs = 0;
    int m_errorStatus = 0;
    QByteArray m_errorBody;
    bool m_jsonEnvelope = false;
    QStringList m_bodies;
    QStringList m_authHeaders;
    QStringList m_xiApiKeys;
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

class TestTtsClient : public QObject {
    Q_OBJECT

private slots:
    void init() { m_server.resetCounters(); }

    void synthesizesAndEmitsAudio();
    void sendsOpenAiPayloadWithApiKey();
    void sendsElevenLabsPayloadWithXiApiKey();
    void sendsMistralPayloadWithVoiceId();
    void unwrapsMistralAudioDataEnvelope();
    void emitsAudioInEnqueueOrder();
    void reportsErrorOnFailure();
    void reportsHttpStatusAndBodyOnServiceError();
    void ignoresEmptyText();
    void cancelDropsQueue();

private:
    FakeTtsServer m_server;
};

void TestTtsClient::synthesizesAndEmitsAudio() {
    QVERIFY(m_server.start());
    TtsClient client(serviceConfig(m_server.port()));
    QSignalSpy audio(&client, &TtsClient::audioReady);
    QSignalSpy failed(&client, &TtsClient::error);

    client.synthesize(QStringLiteral("Hello world."));
    QVERIFY(audio.wait(5000));
    QCOMPARE(audio.first().at(1).toString(), QStringLiteral("audio/wav"));
    QVERIFY(!audio.first().at(0).toByteArray().isEmpty());
    QCOMPARE(failed.count(), 0);
}

void TestTtsClient::sendsOpenAiPayloadWithApiKey() {
    QVERIFY(m_server.start());
    auto config = serviceConfig(m_server.port());
    config.apiKey = QStringLiteral("secret-key");
    config.language = QStringLiteral("de");
    TtsClient client(config);
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("Hallo Welt."));
    QVERIFY(audio.wait(5000));

    QCOMPARE(m_server.requestCount(), 1);
    const QString body = m_server.requestBodies().first();
    QVERIFY(body.contains(QStringLiteral("\"input\":\"Hallo Welt.\"")));
    QVERIFY(body.contains(QStringLiteral("\"model\":\"tts-1\"")));
    QVERIFY(body.contains(QStringLiteral("\"voice\":\"alloy\"")));
    QVERIFY(body.contains(QStringLiteral("\"response_format\":\"mp3\"")));
    QVERIFY(body.contains(QStringLiteral("\"language\":\"de\"")));
    QCOMPARE(m_server.authorizationHeaders().first(), QStringLiteral("Bearer secret-key"));
}

void TestTtsClient::sendsElevenLabsPayloadWithXiApiKey() {
    QVERIFY(m_server.start());
    auto config = serviceConfig(m_server.port());
    config.kind = TtsServiceKind::ElevenLabs;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.port());
    config.apiKey = QStringLiteral("xi-secret");
    config.model = QStringLiteral("eleven_flash_v2_5");
    config.voice = QStringLiteral("voice-id-9");
    TtsClient client(config);
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("Hello."));
    QVERIFY(audio.wait(5000));

    QCOMPARE(m_server.requestCount(), 1);
    const QString body = m_server.requestBodies().first();
    QVERIFY(body.contains(QStringLiteral("\"model_id\":\"eleven_flash_v2_5\"")));
    QVERIFY(body.contains(QStringLiteral("\"text\":\"Hello.\"")));
    QVERIFY(!body.contains(QStringLiteral("\"model\":")));
    QCOMPARE(m_server.xiApiKeyHeaders().first(), QStringLiteral("xi-secret"));
    QVERIFY(m_server.authorizationHeaders().first().isEmpty());
}

void TestTtsClient::sendsMistralPayloadWithVoiceId() {
    // Mistral's speech API takes the voice as "voice_id" (preset slug or
    // saved voice ID), not the OpenAI-style "voice" name; sending "voice"
    // fails with 404 invalid_voice.
    QVERIFY(m_server.start());
    auto config = serviceConfig(m_server.port());
    config.kind = TtsServiceKind::Mistral;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.port());
    config.apiKey = QStringLiteral("mistral-key");
    config.model = QStringLiteral("voxtral-mini-tts-2603");
    config.voice = QStringLiteral("gb_jane_neutral");
    TtsClient client(config);
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("Hello."));
    QVERIFY(audio.wait(5000));

    QCOMPARE(m_server.requestCount(), 1);
    const QString body = m_server.requestBodies().first();
    QVERIFY(body.contains(QStringLiteral("\"model\":\"voxtral-mini-tts-2603\"")));
    QVERIFY(body.contains(QStringLiteral("\"input\":\"Hello.\"")));
    QVERIFY(body.contains(QStringLiteral("\"voice_id\":\"gb_jane_neutral\"")));
    QVERIFY(!body.contains(QStringLiteral("\"voice\":")));
    QCOMPARE(m_server.authorizationHeaders().first(), QStringLiteral("Bearer mistral-key"));
}

void TestTtsClient::unwrapsMistralAudioDataEnvelope() {
    // Mistral's speech endpoint answers 200 with a JSON envelope
    // {"audio_data": "<base64>"}; the client must unwrap it to get the
    // audio bytes, otherwise the response is not decodable audio.
    QVERIFY(m_server.start());
    m_server.setJsonEnvelope(true);
    const QByteArray pcm(320, 0);
    m_server.setAudio(AudioChunker::wavFromPcm(pcm, 16000));
    auto config = serviceConfig(m_server.port());
    config.kind = TtsServiceKind::Mistral;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.port());
    TtsClient client(config);
    QSignalSpy audio(&client, &TtsClient::audioReady);
    QSignalSpy failed(&client, &TtsClient::error);

    client.synthesize(QStringLiteral("Hello."));
    QVERIFY(audio.wait(5000));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(audio.first().at(1).toString(), QStringLiteral("audio/wav"));
    QCOMPARE(audio.first().at(0).toByteArray(), AudioChunker::wavFromPcm(pcm, 16000));
}

void TestTtsClient::emitsAudioInEnqueueOrder() {
    QVERIFY(m_server.start());
    // The response echoes the request's "input" text as WAV PCM data, and
    // the request containing "First" is answered slowly: replies arrive out
    // of order, but audioReady must stay in enqueue order.
    m_server.setEchoInput(true);
    m_server.setSlowMarker(QStringLiteral("First"), 400);
    TtsClient client(serviceConfig(m_server.port()));
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("First."));
    client.synthesize(QStringLiteral("Second."));
    client.synthesize(QStringLiteral("Third."));

    QTRY_VERIFY_WITH_TIMEOUT(audio.size() >= 3, 5000);
    QCOMPARE(audio.size(), 3);
    const auto pcmOf = [](const QByteArray &wav) {
        return QString::fromUtf8(AudioFormat::wavPcmData(wav));
    };
    QCOMPARE(pcmOf(audio.at(0).at(0).toByteArray()), QStringLiteral("First."));
    QCOMPARE(pcmOf(audio.at(1).at(0).toByteArray()), QStringLiteral("Second."));
    QCOMPARE(pcmOf(audio.at(2).at(0).toByteArray()), QStringLiteral("Third."));
}

void TestTtsClient::reportsErrorOnFailure() {
    auto config = serviceConfig(m_server.port());
    // Point at a port where no HTTP server answers.
    config.baseUrl = QStringLiteral("http://127.0.0.1:1/v1");
    TtsClient client(config);
    QSignalSpy failed(&client, &TtsClient::error);

    client.synthesize(QStringLiteral("Hello."));
    QVERIFY(failed.wait(5000));
}

void TestTtsClient::reportsHttpStatusAndBodyOnServiceError() {
    QVERIFY(m_server.start());
    m_server.setErrorResponse(401, QByteArrayLiteral("{\"error\":\"invalid api key\"}"));
    TtsClient client(serviceConfig(m_server.port()));
    QSignalSpy failed(&client, &TtsClient::error);
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("Hello."));
    QVERIFY(failed.wait(5000));
    // The bare QNetworkReply wording ("server replied: ...") is useless for
    // diagnosing a rejected request; the error must carry the HTTP status
    // and the service's error body.
    const QString message = failed.first().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("401")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("invalid api key")), qPrintable(message));
    QCOMPARE(audio.count(), 0);
}

void TestTtsClient::ignoresEmptyText() {
    TtsClient client(serviceConfig(1));
    QSignalSpy audio(&client, &TtsClient::audioReady);
    client.synthesize(QString());
    client.synthesize(QStringLiteral("   "));
    QCOMPARE(audio.count(), 0);
}

void TestTtsClient::cancelDropsQueue() {
    QVERIFY(m_server.start());
    m_server.setDelayMs(300);
    TtsClient client(serviceConfig(m_server.port()));
    QSignalSpy audio(&client, &TtsClient::audioReady);

    client.synthesize(QStringLiteral("One."));
    client.synthesize(QStringLiteral("Two."));
    client.cancel();
    QTest::qWait(600);
    QCOMPARE(audio.count(), 0);
}

QTEST_GUILESS_MAIN(TestTtsClient)
#include "tst_ttsclient.moc"
