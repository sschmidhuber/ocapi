#include "backends/mcpclient.h"

#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

namespace {

McpServerConfig stdioConfig(const QString &command) {
    McpServerConfig config;
    config.name = QStringLiteral("test");
    config.transport = McpTransport::Stdio;
    config.command = command;
    config.enabled = true;
    return config;
}

// A tiny MCP server speaking newline-delimited JSON-RPC over stdio. It
// answers initialize, tools/list, tools/call and prompts/list.
constexpr auto kFakeServer = R"PY(
import json, sys
tools = [{"name": "echo", "description": "Echo text",
          "inputSchema": {"type": "object",
                          "properties": {"text": {"type": "string"}}}}]
prompts = [{"name": "greet", "description": "Greeting",
            "arguments": [{"name": "name", "required": True}]}]
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    msg = json.loads(line)
    method = msg.get("method")
    rid = msg.get("id")
    if method == "initialize":
        result = {"protocolVersion": "2025-06-18",
                  "capabilities": {"tools": {}, "prompts": {}},
                  "serverInfo": {"name": "fake", "version": "0.1"}}
    elif method == "tools/list":
        result = {"tools": tools}
    elif method == "tools/call":
        args = msg["params"]["arguments"]
        result = {"content": [{"type": "text", "text": "echo: " + args.get("text", "")}],
                  "isError": False}
    elif method == "prompts/list":
        result = {"prompts": prompts}
    elif method == "resources/list":
        result = {"resources": []}
    elif method == "resources/templates/list":
        result = {"resourceTemplates": []}
    else:
        if rid is not None:
            print(json.dumps({"jsonrpc": "2.0", "id": rid,
                              "error": {"code": -32601, "message": "unknown"}}),
                  flush=True)
        continue
    if rid is not None:
        print(json.dumps({"jsonrpc": "2.0", "id": rid, "result": result}), flush=True)
)PY";

} // namespace

class TestMcpClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void stdioServerConnectsAndReportsServerInfo();
    void stdioServerListsTools();
    void stdioServerListsPrompts();
    void stdioServerCallsTool();
    void stdioFailureReportsError();
    void httpWithoutUrlFailsImmediately();
    void notConnectedCallsFail();

private:
    [[nodiscard]] static bool pythonAvailable();
    QString m_python;
};

void TestMcpClient::initTestCase() {
    qRegisterMetaType<QVector<McpTool>>();
    qRegisterMetaType<QVector<McpPrompt>>();
    qRegisterMetaType<McpToolResult>();
    qRegisterMetaType<McpServerInfo>();
    m_python = QStringLiteral("python3");
    if (!pythonAvailable())
        QSKIP("python3 is required for the stdio MCP client tests");
}

bool TestMcpClient::pythonAvailable() {
    QProcess process;
    process.start(QStringLiteral("python3"), {QStringLiteral("--version")});
    return process.waitForFinished(5000) && process.exitCode() == 0;
}

void TestMcpClient::stdioServerConnectsAndReportsServerInfo() {
    McpClient client(stdioConfig(m_python));
    // Pass the fake server script via -c.
    client.configRef().arguments = {QStringLiteral("-c"), QString::fromUtf8(kFakeServer)};
    QSignalSpy connectedSpy(&client, &McpClient::connected);
    QSignalSpy failedSpy(&client, &McpClient::connectionFailed);
    client.connectToServer();
    QVERIFY(connectedSpy.wait(10000));
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(client.state(), McpClient::State::Connected);
    QCOMPARE(client.serverInfo().name, QStringLiteral("fake"));
    QCOMPARE(client.serverInfo().version, QStringLiteral("0.1"));
    QVERIFY(client.serverInfo().capabilities.tools);
    QVERIFY(client.serverInfo().capabilities.prompts);
    QVERIFY(!client.serverInfo().capabilities.resources);
}

void TestMcpClient::stdioServerListsTools() {
    McpClient client(stdioConfig(m_python));
    client.configRef().arguments = {QStringLiteral("-c"), QString::fromUtf8(kFakeServer)};
    QSignalSpy connectedSpy(&client, &McpClient::connected);
    QSignalSpy toolsSpy(&client, &McpClient::toolsReady);
    client.connectToServer();
    QVERIFY(connectedSpy.wait(10000));
    // The client fetches tools automatically after connecting.
    QVERIFY(toolsSpy.wait(10000));
    QCOMPARE(toolsSpy.count(), 1);
    const auto tools = toolsSpy.first().at(0).value<QVector<McpTool>>();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.first().name, QStringLiteral("echo"));
    QCOMPARE(tools.first().description, QStringLiteral("Echo text"));
    QVERIFY(tools.first().inputSchema.contains(QStringLiteral("properties")));
    QCOMPARE(tools.first().serverId, client.config().id.toString(QUuid::WithoutBraces));
}

void TestMcpClient::stdioServerListsPrompts() {
    McpClient client(stdioConfig(m_python));
    client.configRef().arguments = {QStringLiteral("-c"), QString::fromUtf8(kFakeServer)};
    QSignalSpy connectedSpy(&client, &McpClient::connected);
    QSignalSpy promptsSpy(&client, &McpClient::promptsReady);
    client.connectToServer();
    QVERIFY(connectedSpy.wait(10000));
    QVERIFY(promptsSpy.wait(10000));
    const auto prompts = promptsSpy.first().at(0).value<QVector<McpPrompt>>();
    QCOMPARE(prompts.size(), 1);
    QCOMPARE(prompts.first().name, QStringLiteral("greet"));
    QCOMPARE(prompts.first().arguments.size(), 1);
    QVERIFY(prompts.first().arguments.first().required);
}

void TestMcpClient::stdioServerCallsTool() {
    McpClient client(stdioConfig(m_python));
    client.configRef().arguments = {QStringLiteral("-c"), QString::fromUtf8(kFakeServer)};
    QSignalSpy connectedSpy(&client, &McpClient::connected);
    QSignalSpy resultSpy(&client, &McpClient::toolResult);
    client.connectToServer();
    QVERIFY(connectedSpy.wait(10000));
    client.callTool(QStringLiteral("echo"),
                    QJsonObject{{QStringLiteral("text"), QStringLiteral("hello")}});
    QVERIFY(resultSpy.wait(10000));
    QCOMPARE(resultSpy.count(), 1);
    QCOMPARE(resultSpy.first().at(0).toString(), QStringLiteral("echo"));
    const auto result = resultSpy.first().at(1).value<McpToolResult>();
    QVERIFY(!result.isError);
    QCOMPARE(result.text, QStringLiteral("echo: hello"));
}

void TestMcpClient::stdioFailureReportsError() {
    McpClient client(stdioConfig(QStringLiteral("/nonexistent/mcp-server-binary")));
    QSignalSpy failedSpy(&client, &McpClient::connectionFailed);
    client.connectToServer();
    QVERIFY(failedSpy.wait(10000));
    QCOMPARE(client.state(), McpClient::State::Failed);
    QVERIFY(!client.lastError().isEmpty());
}

void TestMcpClient::httpWithoutUrlFailsImmediately() {
    McpServerConfig config;
    config.name = QStringLiteral("empty");
    config.transport = McpTransport::StreamableHttp;
    config.url = QString();
    McpClient client(config);
    QSignalSpy failedSpy(&client, &McpClient::connectionFailed);
    client.connectToServer();
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(client.state(), McpClient::State::Failed);
}

void TestMcpClient::notConnectedCallsFail() {
    McpClient client(stdioConfig(QStringLiteral("true")));
    QSignalSpy failedSpy(&client, &McpClient::requestFailed);
    client.listTools();
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), QStringLiteral("tools/list"));
}

QTEST_GUILESS_MAIN(TestMcpClient)
#include "tst_mcpclient.moc"
