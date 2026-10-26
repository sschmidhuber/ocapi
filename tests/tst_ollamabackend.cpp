#include "backends/ollamabackend.h"
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

// Minimal fake Ollama server: one POST endpoint (/api/chat) that replies with
// a scripted Server-Sent Events stream and records the request payload.
class FakeOllamaServer : public QObject {
    Q_OBJECT

public:
    explicit FakeOllamaServer(QObject *parent = nullptr) : QObject(parent) {
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
        for (const QString &event : m_events) {
            body += "data: " + event.toUtf8() + "\n\n";
        }
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

BackendConfig ollamaConfig(quint16 port) {
    BackendConfig config;
    config.name = QStringLiteral("fake");
    config.kind = BackendKind::Ollama;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    config.defaultModel = QStringLiteral("mock-model");
    config.enabled = true;
    return config;
}

} // namespace

class TestOllamaBackend : public QObject {
    Q_OBJECT

private slots:
    void supportsToolsAndAcceptsTools();
    void sendMessageStreamsText();
    void sendMessageReasoningModelParts();
    void sendMessageEmitsToolCalls();
    void toolCallsIncludedInConversationHistory();
    void toolsSentOnlyWhenEnabled();

private:
    FakeOllamaServer m_server;
};

void TestOllamaBackend::supportsToolsAndAcceptsTools() {
    OllamaBackend backend(ollamaConfig(1));
    QVERIFY(backend.supportsTools());
    QJsonArray tools;
    tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                             {QStringLiteral("function"),
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("srv__echo")}}}});
    backend.setAvailableTools(tools);
    // No crash, state accepted.
    backend.setAvailableTools({});
}

void TestOllamaBackend::sendMessageStreamsText() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {QStringLiteral(R"({"message":{"role":"assistant","content":"Hi "}})"),
         QStringLiteral(R"({"message":{"role":"assistant","content":"there"},"done":true})")});

    OllamaBackend backend(ollamaConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("mock-model"));

    // The assistant message is streaming from the moment the backend creates
    // it, before any token arrives.
    QCOMPARE(session.messages().constLast().streaming, true);

    QVERIFY(tokenSpy.wait(10000));
    QTRY_COMPARE_WITH_TIMEOUT(tokenSpy.count(), 2, 10000);
    const QString accumulated = tokenSpy.last().at(1).toString();
    QCOMPARE(accumulated, QStringLiteral("Hi there"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QVERIFY(m_server.lastRequest().contains(QStringLiteral("model")));
}

void TestOllamaBackend::sendMessageReasoningModelParts() {
    // Reasoning-capable models stream "thinking" content parts first, then
    // the first visible token as a typed content-part array, then plain
    // string content (the wire shape observed from GLM reasoning models).
    // The first visible token must not be dropped and the thinking text
    // must not leak into the accumulated reply.
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {QStringLiteral(R"({"message":{"role":"assistant","content":""}})"),
         QStringLiteral(
             R"({"message":{"role":"assistant","content":[{"type":"thinking","thinking":[{"type":"text","text":"Let me think"}],"closed":true}]}})"),
         QStringLiteral(
             R"({"message":{"role":"assistant","content":[{"type":"text","text":"Hello"}]}})"),
         QStringLiteral(
             R"({"message":{"role":"assistant","content":" from the reasoning mock!"},"done":true})")});

    OllamaBackend backend(ollamaConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("answer with reasoning"));
    QSignalSpy tokenSpy(&backend, &IBackend::tokenReceived);
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("mock-model"));

    QVERIFY(tokenSpy.wait(10000));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    const QString accumulated = tokenSpy.last().at(1).toString();
    QCOMPARE(accumulated, QStringLiteral("Hello from the reasoning mock!"));
    QVERIFY(!accumulated.contains(QLatin1String("Let me think")));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestOllamaBackend::sendMessageEmitsToolCalls() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({QStringLiteral(
        R"({"message":{"role":"assistant","content":"","tool_calls":[{"function":{"name":"Mock MCP__echo","arguments":{"text":"hi"}}}]}})")});

    OllamaBackend backend(ollamaConfig(m_server.port()));
    QJsonArray tools;
    tools.append(
        QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                    {QStringLiteral("function"),
                     QJsonObject{{QStringLiteral("name"), QStringLiteral("Mock MCP__echo")}}}});
    backend.setAvailableTools(tools);
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("use the tool"));
    QSignalSpy callsSpy(&backend, &IBackend::toolCallsRequested);
    backend.sendMessage(&session, QStringLiteral("mock-model"));

    QVERIFY(callsSpy.wait(10000));
    QCOMPARE(callsSpy.count(), 1);
    const auto calls = callsSpy.first().at(1).value<QVector<ToolCall>>();
    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.first().name, QStringLiteral("Mock MCP__echo"));
    QCOMPARE(calls.first().arguments.value(QLatin1String("text")).toString(), QStringLiteral("hi"));

    // The tools must have been offered to the server in the request payload.
    const auto requestTools = m_server.lastRequest().value(QLatin1String("tools")).toArray();
    QCOMPARE(requestTools.size(), 1);
}

void TestOllamaBackend::toolCallsIncludedInConversationHistory() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {QStringLiteral(R"({"message":{"role":"assistant","content":"done"},"done":true})")});

    OllamaBackend backend(ollamaConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("use the tool"));
    auto &assistant = session.appendMessage(Role::Assistant);
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("Mock MCP__echo");
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    QVector<ToolCall> calls{call};
    session.setToolCalls(assistant.id, calls);
    Message toolMessage;
    toolMessage.role = Role::Tool;
    toolMessage.toolCallId = QStringLiteral("call-1");
    toolMessage.toolName = QStringLiteral("Mock MCP__echo");
    toolMessage.content = QStringLiteral("echo: hi");
    session.appendMessage(toolMessage);

    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("mock-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);

    // The assistant's tool call and the tool result are replayed in Ollama's
    // message format.
    const auto messages = m_server.lastRequest().value(QLatin1String("messages")).toArray();
    QCOMPARE(messages.size(), 3);
    const auto assistantMessage = messages.at(1).toObject();
    QVERIFY(assistantMessage.contains(QLatin1String("tool_calls")));
    const auto replayedCalls = assistantMessage.value(QLatin1String("tool_calls")).toArray();
    QCOMPARE(replayedCalls.first()
                 .toObject()
                 .value(QLatin1String("function"))
                 .toObject()
                 .value(QLatin1String("name"))
                 .toString(),
             QStringLiteral("Mock MCP__echo"));
    const auto toolMessageJson = messages.at(2).toObject();
    QCOMPARE(toolMessageJson.value(QLatin1String("role")).toString(), QStringLiteral("tool"));
    QCOMPARE(toolMessageJson.value(QLatin1String("content")).toString(),
             QStringLiteral("echo: hi"));
}

void TestOllamaBackend::toolsSentOnlyWhenEnabled() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents(
        {QStringLiteral(R"({"message":{"role":"assistant","content":"ok"},"done":true})")});

    OllamaBackend backend(ollamaConfig(m_server.port()));
    // No tools set: the payload must not contain a "tools" field.
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("mock-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QVERIFY(!m_server.lastRequest().contains(QLatin1String("tools")));
}

QTEST_GUILESS_MAIN(TestOllamaBackend)
#include "tst_ollamabackend.moc"
