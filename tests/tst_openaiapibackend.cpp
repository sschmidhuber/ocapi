#include "backends/openaiapibackend.h"
#include "core/backendconfig.h"
#include "core/chatsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace ocapi;

namespace {

// Minimal fake OpenAI-compatible server: one POST endpoint that replies with
// a scripted Server-Sent Events stream, following the harness shape of
// tst_ollamabackend's FakeOllamaServer.
class FakeOpenAiServer : public QObject {
    Q_OBJECT

public:
    explicit FakeOpenAiServer(QObject *parent = nullptr) : QObject(parent) {
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

    void setStreamEvents(const QStringList &events) { m_events = events; }

private:
    void respondIfComplete(QTcpSocket *socket) {
        const QByteArray &buffer = m_requests[socket];
        const int headerEnd = static_cast<int>(buffer.indexOf("\r\n\r\n"));
        if (headerEnd < 0)
            return;
        const int contentLength = [this, &buffer, headerEnd]() {
            const QString headers = QString::fromLatin1(buffer.left(headerEnd)).toLower();
            const int index = static_cast<int>(headers.indexOf(QLatin1String("content-length:")));
            if (index < 0)
                return -1;
            return headers.mid(index + 16).split(QLatin1String("\r\n")).first().toInt();
        }();
        if (contentLength < 0 || buffer.size() < headerEnd + 4 + contentLength)
            return;

        QByteArray body;
        for (const QString &event : m_events)
            body += "data: " + event.toUtf8() + "\n\n";
        body += "data: [DONE]\n\n";
        QByteArray response;
        response += "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/event-stream\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QStringList m_events;
};

BackendConfig apiConfig(quint16 port) {
    BackendConfig config;
    config.name = QStringLiteral("fake");
    config.kind = BackendKind::OpenAi;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.apiKey = QStringLiteral("test");
    config.defaultModel = QStringLiteral("fake-model");
    config.enabled = true;
    return config;
}

QString chunkJson(const QJsonObject &delta, const QString &finishReason) {
    QJsonObject choice{{QStringLiteral("index"), 0},
                       {QStringLiteral("delta"), delta},
                       {QStringLiteral("finish_reason"),
                        finishReason.isEmpty() ? QJsonValue() : QJsonValue(finishReason)}};
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{choice}}})
            .toJson(QJsonDocument::Compact));
}

} // namespace

class TestOpenAiApiBackend : public QObject {
    Q_OBJECT

private slots:
    void sendMessageStreamsText();
    void contentPartsArrayIsRendered();
    void thinkingPartsAreIgnored();
    void reasoningContentIsStreamed();
    void thinkingPartsAreStreamedAsReasoning();

private:
    FakeOpenAiServer m_server;
};

void TestOpenAiApiBackend::sendMessageStreamsText() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {chunkJson(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                               {QStringLiteral("content"), QString()}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QStringLiteral("Hi ")}}, QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QStringLiteral("there.")}},
                   QStringLiteral("stop"))});

    OpenAiApiBackend backend(apiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));

    QVERIFY(tokenSpy.wait(10000));
    QTRY_COMPARE_WITH_TIMEOUT(tokenSpy.count(), 2, 10000);
    const QString accumulated = tokenSpy.last().at(1).toString();
    QCOMPARE(accumulated, QStringLiteral("Hi there."));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestOpenAiApiBackend::contentPartsArrayIsRendered() {
    // Regression: reasoning-capable servers (e.g. Z.ai's GLM models) stream
    // the first visible token as a content-parts array and switch to plain
    // strings afterwards. The first token must not be dropped.
    QVERIFY(m_server.start());
    const QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("text"), QStringLiteral("I'm")}};
    m_server.setStreamEvents(
        {chunkJson(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                               {QStringLiteral("content"), QString()}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QJsonArray{textPart}}}, QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QStringLiteral(" GLM, a model.")}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QString()}}, QStringLiteral("stop"))});

    OpenAiApiBackend backend(apiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("who are you?"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));

    QVERIFY(tokenSpy.wait(10000));
    QTRY_COMPARE_WITH_TIMEOUT(tokenSpy.count(), 2, 10000);
    const QString accumulated = tokenSpy.last().at(1).toString();
    QCOMPARE(accumulated, QStringLiteral("I'm GLM, a model."));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestOpenAiApiBackend::thinkingPartsAreIgnored() {
    // Reasoning arrives as {"type":"thinking","thinking":[...]} parts; only
    // the visible "text" parts may be shown in the chat.
    QVERIFY(m_server.start());
    const QJsonObject thinkingPart{
        {QStringLiteral("type"), QStringLiteral("thinking")},
        {QStringLiteral("thinking"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                {QStringLiteral("text"), QStringLiteral("Let me think")}}}},
        {QStringLiteral("closed"), true}};
    const QJsonObject textPart{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("text"), QStringLiteral("Answer.")}};
    m_server.setStreamEvents(
        {chunkJson(QJsonObject{{QStringLiteral("content"), QJsonArray{thinkingPart, textPart}}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QString()}}, QStringLiteral("stop"))});

    OpenAiApiBackend backend(apiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));

    QVERIFY(tokenSpy.wait(10000));
    QCOMPARE(tokenSpy.count(), 1);
    QCOMPARE(tokenSpy.first().at(1).toString(), QStringLiteral("Answer."));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestOpenAiApiBackend::reasoningContentIsStreamed() {
    // DeepSeek-style servers stream reasoning as "reasoning_content" delta
    // fields before the visible answer; both must arrive on their signals.
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {chunkJson(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                               {QStringLiteral("content"), QString()}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("reasoning_content"), QStringLiteral("Pondering ")}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("reasoning_content"), QStringLiteral("hard.")}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QStringLiteral("Answer.")}},
                   QStringLiteral("stop"))});

    OpenAiApiBackend backend(apiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy reasoningSpy(&backend, &IBackend::reasoningReceived);
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));

    QVERIFY(reasoningSpy.wait(10000));
    QTRY_COMPARE_WITH_TIMEOUT(reasoningSpy.count(), 2, 10000);
    QCOMPARE(reasoningSpy.last().at(1).toString(), QStringLiteral("Pondering hard."));
    QTRY_VERIFY_WITH_TIMEOUT(tokenSpy.count() >= 1, 10000);
    QCOMPARE(tokenSpy.first().at(1).toString(), QStringLiteral("Answer."));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestOpenAiApiBackend::thinkingPartsAreStreamedAsReasoning() {
    // GLM-style servers stream reasoning as thinking parts of the content
    // array; the reasoning text must not leak into the visible answer.
    QVERIFY(m_server.start());
    const QJsonObject thinkingPart{{QStringLiteral("type"), QStringLiteral("thinking")},
                                   {QStringLiteral("thinking"), QStringLiteral("Let me think")}};
    m_server.setStreamEvents(
        {chunkJson(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                               {QStringLiteral("content"), QString()}},
                   QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QJsonArray{thinkingPart}}}, QString()),
         chunkJson(QJsonObject{{QStringLiteral("content"), QStringLiteral("Answer.")}},
                   QStringLiteral("stop"))});

    OpenAiApiBackend backend(apiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy reasoningSpy(&backend, &IBackend::reasoningReceived);
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));

    QVERIFY(reasoningSpy.wait(10000));
    QCOMPARE(reasoningSpy.first().at(1).toString(), QStringLiteral("Let me think"));
    QTRY_VERIFY_WITH_TIMEOUT(tokenSpy.count() >= 1, 10000);
    QCOMPARE(tokenSpy.first().at(1).toString(), QStringLiteral("Answer."));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

QTEST_GUILESS_MAIN(TestOpenAiApiBackend)
#include "tst_openaiapibackend.moc"
