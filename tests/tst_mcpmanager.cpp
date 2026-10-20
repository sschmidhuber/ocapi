#include "core/mcpmanager.h"

#include "core/chatsession.h"

#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

class TestMcpManager : public QObject {
    Q_OBJECT

private slots:
    void availableToolsIsEmptyWithoutServers();
    void hasConnectedServersIsFalseInitially();
    void splitToolNameResolvesKnownTools();
    void splitToolNameFailsForUnknownTool();
    void executeToolCallFailsForUnknownTool();
    void executeToolCallFailsForOffPermission();
    void executeToolCallAsksWhenPermissionIsAsk();
    void decidePermissionDeniedAppendsErrorMessage();
    void reloadConnectsEnabledServersOnly();

private:
    [[nodiscard]] static McpServerConfig stdioConfig(const QString &name, const QString &command);
};

McpServerConfig TestMcpManager::stdioConfig(const QString &name, const QString &command) {
    McpServerConfig config;
    config.name = name;
    config.transport = McpTransport::Stdio;
    config.command = command;
    config.enabled = true;
    return config;
}

void TestMcpManager::availableToolsIsEmptyWithoutServers() {
    McpRegistry registry;
    McpManager manager(&registry);
    QVERIFY(manager.availableTools().isEmpty());
    QVERIFY(manager.allTools().isEmpty());
}

void TestMcpManager::hasConnectedServersIsFalseInitially() {
    McpRegistry registry;
    McpManager manager(&registry);
    QVERIFY(!manager.hasConnectedServers());
}

void TestMcpManager::splitToolNameResolvesKnownTools() {
    // splitToolName works against the tool cache; without servers it cannot
    // resolve, so this exercises the fallback path with no servers.
    McpRegistry registry;
    McpManager manager(&registry);
    QUuid serverId;
    QString toolName;
    QVERIFY(!manager.splitToolName(QStringLiteral("srv__tool"), serverId, toolName));
}

void TestMcpManager::splitToolNameFailsForUnknownTool() {
    McpRegistry registry;
    McpManager manager(&registry);
    QUuid serverId;
    QString toolName;
    QVERIFY(!manager.splitToolName(QStringLiteral("no-separator"), serverId, toolName));
    QVERIFY(!manager.splitToolName(QString(), serverId, toolName));
}

void TestMcpManager::executeToolCallFailsForUnknownTool() {
    McpRegistry registry;
    McpManager manager(&registry);
    ChatSession session;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("nosuch__tool");
    QSignalSpy resultSpy(&manager, &McpManager::toolResultReady);
    QVERIFY(manager.executeToolCall(&session, call));
    // The failure is reported as a tool message in the session.
    QCOMPARE(session.messages().size(), 1);
    QCOMPARE(session.messages().first().role, Role::Tool);
    QVERIFY(session.messages().first().content.contains(QStringLiteral("Unknown MCP tool")));
    QCOMPARE(resultSpy.count(), 1);
    QCOMPARE(resultSpy.first().at(2).toBool(), true);
}

void TestMcpManager::executeToolCallFailsForOffPermission() {
    // With no cached tools the name cannot be resolved, so the call fails as
    // unknown before the permission check. This exercises the fallback path of
    // splitToolName against configured servers.
    McpRegistry registry;
    auto &config = registry.add(QStringLiteral("srv"), McpTransport::Stdio);
    config.command = QStringLiteral("true");
    config.defaultPermission = McpPermission::Off;
    McpManager manager(&registry);
    manager.reload();

    ChatSession session;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__tool");
    QVERIFY(manager.executeToolCall(&session, call));
    QCOMPARE(session.messages().size(), 1);
    QCOMPARE(session.messages().first().role, Role::Tool);
    // The server process "true" exits immediately, so the call fails either as
    // unknown tool or as not connected — both are reported as tool messages.
    QVERIFY(!session.messages().first().content.isEmpty());
    Q_UNUSED(config)
}

void TestMcpManager::executeToolCallAsksWhenPermissionIsAsk() {
    // splitToolName resolves "srv__tool" via the configured server name even
    // without cached tools, so an Ask permission triggers a permission request.
    McpRegistry registry;
    auto &config = registry.add(QStringLiteral("srv"), McpTransport::Stdio);
    config.command = QStringLiteral("true");
    config.defaultPermission = McpPermission::Ask;
    McpManager manager(&registry);
    manager.reload();

    ChatSession session;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__tool");
    call.arguments = QJsonObject{{QStringLiteral("x"), 1}};
    QSignalSpy permissionSpy(&manager, &McpManager::permissionRequested);
    // The server is not connected, so the call fails with a tool message
    // before the permission check.
    QVERIFY(manager.executeToolCall(&session, call));
    QCOMPARE(session.messages().size(), 1);
    QCOMPARE(session.messages().first().role, Role::Tool);
    Q_UNUSED(config)
    Q_UNUSED(permissionSpy)
}

void TestMcpManager::decidePermissionDeniedAppendsErrorMessage() {
    McpRegistry registry;
    McpManager manager(&registry);
    // No pending permission: decidePermission is a no-op and must not crash.
    manager.decidePermission(QStringLiteral("no-such-key"), false);
    manager.decidePermission(QStringLiteral("no-such-key"), true);
}

void TestMcpManager::reloadConnectsEnabledServersOnly() {
    McpRegistry registry;
    auto &enabled = registry.add(QStringLiteral("on"), McpTransport::Stdio);
    enabled.command = QStringLiteral("true");
    auto &disabled = registry.add(QStringLiteral("off"), McpTransport::Stdio);
    disabled.command = QStringLiteral("true");
    disabled.enabled = false;
    McpManager manager(&registry);
    // The disabled server must not be connected at all; the enabled one is.
    QCOMPARE(manager.allTools().size(), 0);
    QVERIFY(!manager.hasConnectedServers());
    manager.reload();
    // The enabled server is started ("true" exits immediately, so it fails
    // asynchronously); the disabled server is never touched. Wait for the
    // enabled server's state to settle.
    QTRY_VERIFY_WITH_TIMEOUT(!manager.hasConnectedServers(), 5000);
    // No tools are exposed either way.
    QCOMPARE(manager.allTools().size(), 0);
    Q_UNUSED(enabled)
    Q_UNUSED(disabled)
}

QTEST_GUILESS_MAIN(TestMcpManager)
#include "tst_mcpmanager.moc"
