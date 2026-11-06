#include "backends/anthropicbackend.h"
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

// Minimal fake Anthropic server: one POST /messages endpoint replying with a
// scripted SSE stream (event: <name>\ndata: <json>), same harness shape as
// tst_openaiapibackend's FakeOpenAiServer.
class FakeAnthropicServer : public QObject {
    Q_OBJECT

public:
    explicit FakeAnthropicServer(QObject *parent = nullptr) : QObject(parent) {
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

    // Full raw SSE events, e.g. "event: message_stop\ndata: {}\n\n".
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
            body += event.toUtf8() + "\n\n";
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

BackendConfig anthropicConfig(quint16 port) {
    BackendConfig config;
    config.name = QStringLiteral("fake");
    config.kind = BackendKind::Anthropic;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    config.apiKey = QStringLiteral("test");
    config.defaultModel = QStringLiteral("fake-model");
    config.enabled = true;
    return config;
}

QString sseEvent(const QString &name, const QJsonObject &data) {
    return QStringLiteral("event: %1\ndata: %2")
        .arg(name, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
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

class TestAnthropicBackend : public QObject {
    Q_OBJECT

private slots:
    void toolsSentInAnthropicFormat();
    void toolsNotSentWhenDisabledForBackend();
    void toolUseStreamEmitsToolCallsRequested();
    void toolHistoryIsReplayedInAnthropicFormat();

private:
    FakeAnthropicServer m_server;
};

void TestAnthropicBackend::toolsSentInAnthropicFormat() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({sseEvent(QStringLiteral("message_stop"), QJsonObject{})});

    AnthropicBackend backend(anthropicConfig(m_server.port()));
    backend.setAvailableTools(openAiShapedTools());
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);

    const auto tools = m_server.lastRequest().value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    const auto tool = tools.first().toObject();
    QCOMPARE(tool.value(QStringLiteral("name")).toString(), QStringLiteral("srv__echo"));
    QCOMPARE(tool.value(QStringLiteral("description")).toString(), QStringLiteral("Echo text"));
    QVERIFY(tool.contains(QStringLiteral("input_schema")));
    QVERIFY(!tool.contains(QStringLiteral("function")));
}

void TestAnthropicBackend::toolsNotSentWhenDisabledForBackend() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({sseEvent(QStringLiteral("message_stop"), QJsonObject{})});

    BackendConfig config = anthropicConfig(m_server.port());
    config.toolsEnabled = false;
    AnthropicBackend backend(config);
    backend.setAvailableTools(openAiShapedTools());
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QVERIFY(!m_server.lastRequest().contains(QStringLiteral("tools")));
}

void TestAnthropicBackend::toolUseStreamEmitsToolCallsRequested() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({
        sseEvent(QStringLiteral("message_start"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("message")}}),
        sseEvent(QStringLiteral("content_block_start"),
                 QJsonObject{{QStringLiteral("index"), 0},
                             {QStringLiteral("content_block"),
                              QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                          {QStringLiteral("id"), QStringLiteral("toolu_1")},
                                          {QStringLiteral("name"), QStringLiteral("srv__echo")},
                                          {QStringLiteral("input"), QJsonObject{}}}}}),
        sseEvent(
            QStringLiteral("content_block_delta"),
            QJsonObject{
                {QStringLiteral("index"), 0},
                {QStringLiteral("delta"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                             {QStringLiteral("partial_json"), QStringLiteral("{\"text\":")}}}}),
        sseEvent(QStringLiteral("content_block_delta"),
                 QJsonObject{
                     {QStringLiteral("index"), 0},
                     {QStringLiteral("delta"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")},
                                  {QStringLiteral("partial_json"), QStringLiteral("\"hi\"}")}}}}),
        sseEvent(QStringLiteral("content_block_stop"), QJsonObject{{QStringLiteral("index"), 0}}),
        sseEvent(QStringLiteral("message_stop"), QJsonObject{}),
    });

    AnthropicBackend backend(anthropicConfig(m_server.port()));
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
    QCOMPARE(calls.first().id, QStringLiteral("toolu_1"));
    QCOMPARE(calls.first().name, QStringLiteral("srv__echo"));
    QCOMPARE(calls.first().arguments.value(QStringLiteral("text")).toString(),
             QStringLiteral("hi"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
}

void TestAnthropicBackend::toolHistoryIsReplayedInAnthropicFormat() {
    QVERIFY(m_server.start());
    m_server.setStreamEvents({sseEvent(QStringLiteral("message_stop"), QJsonObject{})});

    AnthropicBackend backend(anthropicConfig(m_server.port()));
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("use the tool"));
    auto &assistant = session.appendMessage(Role::Assistant);
    ToolCall call;
    call.id = QStringLiteral("toolu_1");
    call.name = QStringLiteral("srv__echo");
    call.arguments = QJsonObject{{QStringLiteral("text"), QStringLiteral("hi")}};
    session.setToolCalls(assistant.id, {call});
    Message toolMessage;
    toolMessage.role = Role::Tool;
    toolMessage.toolCallId = QStringLiteral("toolu_1");
    toolMessage.toolName = QStringLiteral("srv__echo");
    toolMessage.content = QStringLiteral("echo: hi");
    session.appendMessage(toolMessage);

    QSignalSpy finishedSpy(&backend, &IBackend::finished);
    backend.sendMessage(&session, QStringLiteral("fake-model"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);

    const auto messages = m_server.lastRequest().value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 3);
    // The assistant's earlier call is replayed as a tool_use block.
    const auto assistantMsg = messages.at(1).toObject();
    QCOMPARE(assistantMsg.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
    bool sawToolUse = false;
    for (const auto &part : assistantMsg.value(QStringLiteral("content")).toArray()) {
        const auto p = part.toObject();
        if (p.value(QStringLiteral("type")).toString() == QStringLiteral("tool_use")) {
            sawToolUse = true;
            QCOMPARE(p.value(QStringLiteral("id")).toString(), QStringLiteral("toolu_1"));
            QCOMPARE(p.value(QStringLiteral("name")).toString(), QStringLiteral("srv__echo"));
            QCOMPARE(p.value(QStringLiteral("input"))
                         .toObject()
                         .value(QStringLiteral("text"))
                         .toString(),
                     QStringLiteral("hi"));
        }
    }
    QVERIFY(sawToolUse);
    // The tool result is a user-role tool_result block keyed by the call id.
    const auto toolMsg = messages.at(2).toObject();
    QCOMPARE(toolMsg.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const auto resultBlock = toolMsg.value(QStringLiteral("content")).toArray().first().toObject();
    QCOMPARE(resultBlock.value(QStringLiteral("type")).toString(), QStringLiteral("tool_result"));
    QCOMPARE(resultBlock.value(QStringLiteral("tool_use_id")).toString(),
             QStringLiteral("toolu_1"));
    QCOMPARE(resultBlock.value(QStringLiteral("content")).toString(), QStringLiteral("echo: hi"));
}

QTEST_GUILESS_MAIN(TestAnthropicBackend)
#include "tst_anthropicbackend.moc"
