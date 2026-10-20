#include "core/mcpprotocol.h"

#include <QJsonDocument>
#include <QtTest>

using namespace ocapi;

class TestMcpProtocol : public QObject {
    Q_OBJECT

private slots:
    void namespacedToolNameJoinsServerAndTool();
    void toolRoundTripsThroughJson();
    void toolsToJsonProducesArray();
    void resourceParsesAllFields();
    void resourceTemplateParsesAllFields();
    void promptParsesArguments();
    void capabilitiesDetectAllSections();
    void serverInfoParsesNameVersionCapabilities();
    void initializeParamsContainProtocolVersionAndClientInfo();
    void toolResultJoinsTextContent();
    void toolResultFallsBackToRawContentJson();
    void jsonRpcRequestHasAllFields();
    void jsonRpcNotificationHasNoId();
    void jsonRpcErrorDetection();
    void nextRequestIdIsUnique();
};

void TestMcpProtocol::namespacedToolNameJoinsServerAndTool() {
    QCOMPARE(namespacedToolName(QStringLiteral("fs"), QStringLiteral("read_file")),
             QStringLiteral("fs__read_file"));
}

void TestMcpProtocol::toolRoundTripsThroughJson() {
    McpTool tool;
    tool.name = QStringLiteral("read_file");
    tool.description = QStringLiteral("Read a file");
    tool.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    tool.serverId = QStringLiteral("server-1");

    const QJsonObject json = toolToJson(tool);
    const McpTool parsed = toolFromJson(json, QStringLiteral("server-1"));
    QCOMPARE(parsed.name, tool.name);
    QCOMPARE(parsed.description, tool.description);
    QCOMPARE(parsed.inputSchema, tool.inputSchema);
    QCOMPARE(parsed.serverId, QStringLiteral("server-1"));
}

void TestMcpProtocol::toolsToJsonProducesArray() {
    QVector<McpTool> tools;
    McpTool tool;
    tool.name = QStringLiteral("a");
    tools.push_back(tool);
    tool.name = QStringLiteral("b");
    tools.push_back(tool);
    const QJsonArray array = toolsToJson(tools);
    QCOMPARE(array.size(), 2);
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("a"));
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("b"));
}

void TestMcpProtocol::resourceParsesAllFields() {
    const QJsonObject object{
        {QStringLiteral("uri"), QStringLiteral("file:///tmp/a.txt")},
        {QStringLiteral("name"), QStringLiteral("a.txt")},
        {QStringLiteral("description"), QStringLiteral("A file")},
        {QStringLiteral("mimeType"), QStringLiteral("text/plain")},
    };
    const McpResource resource = resourceFromJson(object, QStringLiteral("s1"));
    QCOMPARE(resource.uri, QStringLiteral("file:///tmp/a.txt"));
    QCOMPARE(resource.name, QStringLiteral("a.txt"));
    QCOMPARE(resource.description, QStringLiteral("A file"));
    QCOMPARE(resource.mimeType, QStringLiteral("text/plain"));
    QCOMPARE(resource.serverId, QStringLiteral("s1"));
}

void TestMcpProtocol::resourceTemplateParsesAllFields() {
    const QJsonObject object{
        {QStringLiteral("uriTemplate"), QStringLiteral("file:///{path}")},
        {QStringLiteral("name"), QStringLiteral("Files")},
        {QStringLiteral("description"), QStringLiteral("Any file")},
        {QStringLiteral("mimeType"), QStringLiteral("text/plain")},
    };
    const McpResourceTemplate templ = resourceTemplateFromJson(object, QStringLiteral("s1"));
    QCOMPARE(templ.uriTemplate, QStringLiteral("file:///{path}"));
    QCOMPARE(templ.name, QStringLiteral("Files"));
    QCOMPARE(templ.mimeType, QStringLiteral("text/plain"));
    QCOMPARE(templ.serverId, QStringLiteral("s1"));
}

void TestMcpProtocol::promptParsesArguments() {
    const QJsonObject object{
        {QStringLiteral("name"), QStringLiteral("summarize")},
        {QStringLiteral("description"), QStringLiteral("Summarize text")},
        {QStringLiteral("title"), QStringLiteral("Summarize")},
        {QStringLiteral("arguments"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("text")},
                 {QStringLiteral("description"), QStringLiteral("Text to summarize")},
                 {QStringLiteral("required"), true},
             },
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("lang")},
             },
         }},
    };
    const McpPrompt prompt = promptFromJson(object, QStringLiteral("s1"));
    QCOMPARE(prompt.name, QStringLiteral("summarize"));
    QCOMPARE(prompt.title, QStringLiteral("Summarize"));
    QCOMPARE(prompt.arguments.size(), 2);
    QCOMPARE(prompt.arguments.at(0).name, QStringLiteral("text"));
    QVERIFY(prompt.arguments.at(0).required);
    QCOMPARE(prompt.arguments.at(1).name, QStringLiteral("lang"));
    QVERIFY(!prompt.arguments.at(1).required);
    QCOMPARE(prompt.serverId, QStringLiteral("s1"));
}

void TestMcpProtocol::capabilitiesDetectAllSections() {
    const QJsonObject object{
        {QStringLiteral("tools"), QJsonObject{}},
        {QStringLiteral("resources"), QJsonObject{}},
        {QStringLiteral("prompts"), QJsonObject{}},
        {QStringLiteral("logging"), QJsonObject{}},
    };
    const McpCapabilities caps = capabilitiesFromJson(object);
    QVERIFY(caps.tools);
    QVERIFY(caps.resources);
    QVERIFY(caps.prompts);
    QVERIFY(caps.logging);
    QVERIFY(!caps.sampling);

    const McpCapabilities empty = capabilitiesFromJson(QJsonObject{});
    QVERIFY(!empty.tools);
    QVERIFY(!empty.resources);
    QVERIFY(!empty.prompts);
}

void TestMcpProtocol::serverInfoParsesNameVersionCapabilities() {
    const QJsonObject result{
        {QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
        {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
        {QStringLiteral("serverInfo"),
         QJsonObject{
             {QStringLiteral("name"), QStringLiteral("fs")},
             {QStringLiteral("version"), QStringLiteral("1.2.3")},
             {QStringLiteral("title"), QStringLiteral("Filesystem")},
         }},
    };
    const McpServerInfo info = serverInfoFromJson(result);
    QCOMPARE(info.name, QStringLiteral("fs"));
    QCOMPARE(info.version, QStringLiteral("1.2.3"));
    QCOMPARE(info.title, QStringLiteral("Filesystem"));
    QVERIFY(info.capabilities.tools);
    QVERIFY(!info.capabilities.prompts);
}

void TestMcpProtocol::initializeParamsContainProtocolVersionAndClientInfo() {
    const QJsonObject params = initializeParamsJson();
    QCOMPARE(params.value(QStringLiteral("protocolVersion")).toString(),
             QString::fromLatin1(kMcpProtocolVersion));
    const QJsonObject clientInfo = params.value(QStringLiteral("clientInfo")).toObject();
    QCOMPARE(clientInfo.value(QStringLiteral("name")).toString(), QStringLiteral("ocapi"));
    QVERIFY(params.contains(QStringLiteral("capabilities")));
}

void TestMcpProtocol::toolResultJoinsTextContent() {
    const QJsonObject result{
        {QStringLiteral("content"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("text")},
                 {QStringLiteral("text"), QStringLiteral("line1")},
             },
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("text")},
                 {QStringLiteral("text"), QStringLiteral("line2")},
             },
         }},
        {QStringLiteral("isError"), false},
    };
    const McpToolResult parsed = toolResultFromJson(result);
    QVERIFY(!parsed.isError);
    QCOMPARE(parsed.text, QStringLiteral("line1\nline2"));
    QCOMPARE(toolResultToMessageContent(parsed), QStringLiteral("line1\nline2"));
}

void TestMcpProtocol::toolResultFallsBackToRawContentJson() {
    const QJsonObject result{
        {QStringLiteral("content"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("image")},
                 {QStringLiteral("data"), QStringLiteral("…")},
             },
         }},
        {QStringLiteral("isError"), true},
    };
    const McpToolResult parsed = toolResultFromJson(result);
    QVERIFY(parsed.isError);
    QVERIFY(parsed.text.isEmpty());
    const QString content = toolResultToMessageContent(parsed);
    QVERIFY(content.contains(QStringLiteral("image")));
}

void TestMcpProtocol::jsonRpcRequestHasAllFields() {
    const QJsonObject request = jsonRpcRequest(QStringLiteral("tools/list"), QJsonObject{},
                                               QJsonValue(QStringLiteral("id-1")));
    QCOMPARE(request.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(request.value(QStringLiteral("method")).toString(), QStringLiteral("tools/list"));
    QCOMPARE(request.value(QStringLiteral("id")).toString(), QStringLiteral("id-1"));
    QVERIFY(request.contains(QStringLiteral("params")));
}

void TestMcpProtocol::jsonRpcNotificationHasNoId() {
    const QJsonObject notification =
        jsonRpcNotification(QStringLiteral("notifications/initialized"), QJsonObject{});
    QVERIFY(!notification.contains(QStringLiteral("id")));
    QCOMPARE(notification.value(QStringLiteral("method")).toString(),
             QStringLiteral("notifications/initialized"));
}

void TestMcpProtocol::jsonRpcErrorDetection() {
    const QJsonObject errorMessage{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QJsonValue(QStringLiteral("id-1"))},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("code"), -32601},
             {QStringLiteral("message"), QStringLiteral("Method not found")},
         }},
    };
    QVERIFY(isJsonRpcError(errorMessage));
    QCOMPARE(jsonRpcErrorText(errorMessage), QStringLiteral("Method not found (code -32601)"));

    const QJsonObject okMessage{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QJsonValue(QStringLiteral("id-1"))},
        {QStringLiteral("result"), QJsonObject{}},
    };
    QVERIFY(!isJsonRpcError(okMessage));
}

void TestMcpProtocol::nextRequestIdIsUnique() {
    QVERIFY(nextRequestId() != nextRequestId());
}

QTEST_GUILESS_MAIN(TestMcpProtocol)
#include "tst_mcpprotocol.moc"
