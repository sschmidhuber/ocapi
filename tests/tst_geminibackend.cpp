#include "backends/geminibackend.h"
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

// Minimal fake Gemini server: one POST endpoint replying with a scripted SSE
// stream, same harness shape as tst_openaiapibackend's FakeOpenAiServer.
class FakeGeminiServer : public QObject {
    Q_OBJECT

public:
    explicit FakeGeminiServer(QObject *parent = nullptr) : QObject(parent) {
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

    // Raw SSE data payloads (one JSON object per event).
    void setStreamEvents(const QStringList &events) { m_events = events; }

    [[nodiscard]] QJsonObject lastRequest() const { return m_lastRequest; }

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
        m_lastRequest = QJsonDocument::fromJson(buffer.mid(headerEnd + 4, contentLength)).object();

        QByteArray body;
        for (const QString &event : m_events)
            body += "data: " + event.toUtf8() + "\n\n";
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
    QJsonObject m_lastRequest;
};

BackendConfig geminiConfig(quint16 port) {
    BackendConfig config;
    config.name = QStringLiteral("fake");
    config.kind = BackendKind::Gemini;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1beta").arg(port);
    config.apiKey = QStringLiteral("test");
    config.defaultModel = QStringLiteral("fake-model");
    config.enabled = true;
    return config;
}

QJsonObject geminiChunk(const QJsonArray &parts, const QString &finishReason = QString()) {
    QJsonObject candidate{
        {QStringLiteral("content"), QJsonObject{{QStringLiteral("role"), QStringLiteral("model")},
                                                {QStringLiteral("parts"), parts}}},
    };
    if (!finishReason.isEmpty())
        candidate.insert(QStringLiteral("finishReason"), finishReason);
    return QJsonObject{{QStringLiteral("candidates"), QJsonArray{candidate}}};
}

QString jsonString(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonArray openAiShapedTools() {
    return QJsonArray{QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("srv__echo")},
                     {QStringLiteral("description"), QStringLiteral("Echo text")},
                     {QStringLiteral("parameters"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                  {QStringLiteral("properties"),
                                   QJsonObject{{QStringLiteral("text"),
                                                QJsonObject{{QStringLiteral("type"),
                                                             QStringLiteral("string")}}}}}}}}},
    }};
}

} // namespace

class TestGeminiBackend : public QObject {
    Q_OBJECT

private slots:
    void toolsSentAsFunctionDeclarations();
    void toolsNotSentWhenDisabledForBackend();
    void functionCallPartEmitsToolCallsRequested();
    void functionHistoryIsReplayed();

private:
    FakeGeminiServer m_server;
};

void TestGeminiBackend::toolsSentAsFunctionDeclarations() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({jsonString(geminiChunk({}, QStringLiteral("STOP")))});

    GeminiBackend backend(geminiConfig(m_server.port()));
    backend.setAvailableTools(openAiShapedTools());
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);

    const auto tools = m_server.lastRequest().value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    const auto declarations =
        tools.first().toObject().value(QStringLiteral("functionDeclarations")).toArray();
    QCOMPARE(declarations.size(), 1);
    QCOMPARE(declarations.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("srv__echo"));
    QVERIFY(declarations.first().toObject().contains(QStringLiteral("parameters")));
}

void TestGeminiBackend::toolsNotSentWhenDisabledForBackend() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({jsonString(geminiChunk({}, QStringLiteral("STOP")))});

    BackendConfig config = geminiConfig(m_server.port());
    config.toolsEnabled = false;
    GeminiBackend backend(config);
    backend.setAvailableTools(openAiShapedTools());
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QVERIFY(!m_server.lastRequest().contains(QStringLiteral("tools")));
}

void TestGeminiBackend::functionCallPartEmitsToolCallsRequested() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({jsonString(geminiChunk(
        QJsonArray{QJsonObject{
            {QStringLiteral("functionCall"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("srv__echo")},
                         {QStringLiteral("args"),
                          QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}}}}}}},
        QStringLiteral("STOP")))});

    GeminiBackend backend(geminiConfig(m_server.port()));
    backend.setAvailableTools(openAiShapedTools());
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("use the tool"));
    QSignalSpy callsSpy(&backend, &IBackend::toolCallsRequested);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QVERIFY(callsSpy.wait(10000));
    QCOMPARE(callsSpy.count(), 1);
    const auto calls = callsSpy.first().at(1).value<QVector<ToolCall>>();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().name, QStringLiteral("srv__echo"));
    QCOMPARE(calls.first().arguments.value(QStringLiteral("text")).toString(),
             QStringLiteral("hi"));
    QVERIFY(!calls.first().id.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestGeminiBackend::functionHistoryIsReplayed() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({jsonString(geminiChunk({}, QStringLiteral("STOP")))});

    GeminiBackend backend(geminiConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("use the tool"));
    auto &assistant = session.appendMessage(Role::Assistant);
    ToolCall call;
    call.id = QStringLiteral("call_1");
    call.name = QStringLiteral("srv__echo");
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    session.setToolCalls(assistant.id, {call});
    Message toolMessage;
    toolMessage.role = Role::Tool;
    toolMessage.toolCallId = QStringLiteral("call_1");
    toolMessage.toolName = QStringLiteral("srv__echo");
    toolMessage.content = QStringLiteral("echo: hi");
    session.appendMessage(toolMessage);

    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);

    const auto contents = m_server.lastRequest().value(QStringLiteral("contents")).toArray();
    QCOMPARE(contents.size(), 3);
    // The model's earlier call is replayed as a functionCall part on the
    // model role.
    const auto modelMsg = contents.at(1).toObject();
    QCOMPARE(modelMsg.value(QStringLiteral("role")).toString(), QStringLiteral("model"));
    bool sawFunctionCall = false;
    for (const auto &part : modelMsg.value(QStringLiteral("parts")).toArray()) {
        const auto fc = part.toObject().value(QStringLiteral("functionCall")).toObject();
        if (!fc.isEmpty()) {
            sawFunctionCall = true;
            QCOMPARE(fc.value(QStringLiteral("name")).toString(), QStringLiteral("srv__echo"));
            QCOMPARE(fc.value(QStringLiteral("args"))
                         .toObject()
                         .value(QStringLiteral("text"))
                         .toString(),
                     QStringLiteral("hi"));
        }
    }
    QVERIFY(sawFunctionCall);
    // The tool result is a functionResponse part on the user role.
    const auto userMsg = contents.at(2).toObject();
    QCOMPARE(userMsg.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const auto fr = userMsg.value(QStringLiteral("parts"))
                        .toArray()
                        .first()
                        .toObject()
                        .value(QStringLiteral("functionResponse"))
                        .toObject();
    QVERIFY(!fr.isEmpty());
    QCOMPARE(fr.value(QStringLiteral("name")).toString(), QStringLiteral("srv__echo"));
    QCOMPARE(
        fr.value(QStringLiteral("response")).toObject().value(QStringLiteral("result")).toString(),
        QStringLiteral("echo: hi"));
}

QTEST_GUILESS_MAIN(TestGeminiBackend)
#include "tst_geminibackend.moc"
