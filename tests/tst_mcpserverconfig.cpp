#include "core/mcpserverconfig.h"

#include <QtTest>

using namespace ocapi;

class TestMcpServerConfig : public QObject {
    Q_OBJECT

private slots:
    void transportRoundTripsThroughKey();
    void transportFromStringDefaultsToHttp();
    void allTransportsContainsAllKinds();
    void permissionRoundTripsThroughKey();
    void permissionFromStringDefaultsToAsk();
    void permissionForToolFallsBackToServerDefault();
    void permissionForToolUsesPerToolOverride();
    void disabledServerForcesOff();
    void setPermissionForToolStoresOverride();
};

void TestMcpServerConfig::transportRoundTripsThroughKey() {
    for (const auto transport : McpServerConfig::allTransports()) {
        QCOMPARE(McpServerConfig::transportFromString(McpServerConfig::transportKey(transport)),
                 transport);
    }
}

void TestMcpServerConfig::transportFromStringDefaultsToHttp() {
    QCOMPARE(McpServerConfig::transportFromString(QStringLiteral("bogus")),
             McpTransport::StreamableHttp);
    QCOMPARE(McpServerConfig::transportFromString(QString()), McpTransport::StreamableHttp);
}

void TestMcpServerConfig::allTransportsContainsAllKinds() {
    const auto &transports = McpServerConfig::allTransports();
    QVERIFY(transports.contains(McpTransport::Stdio));
    QVERIFY(transports.contains(McpTransport::StreamableHttp));
    QVERIFY(transports.contains(McpTransport::HttpSse));
    QCOMPARE(transports.size(), 3);
}

void TestMcpServerConfig::permissionRoundTripsThroughKey() {
    for (const auto permission : McpServerConfig::allPermissions()) {
        QCOMPARE(McpServerConfig::permissionFromString(McpServerConfig::permissionKey(permission)),
                 permission);
    }
}

void TestMcpServerConfig::permissionFromStringDefaultsToAsk() {
    QCOMPARE(McpServerConfig::permissionFromString(QStringLiteral("bogus")), McpPermission::Ask);
    QCOMPARE(McpServerConfig::permissionFromString(QString()), McpPermission::Ask);
}

void TestMcpServerConfig::permissionForToolFallsBackToServerDefault() {
    McpServerConfig config;
    config.defaultPermission = McpPermission::Allow;
    QCOMPARE(config.permissionForTool(QStringLiteral("any")), McpPermission::Allow);
}

void TestMcpServerConfig::permissionForToolUsesPerToolOverride() {
    McpServerConfig config;
    config.defaultPermission = McpPermission::Allow;
    config.setPermissionForTool(QStringLiteral("danger"), McpPermission::Off);
    QCOMPARE(config.permissionForTool(QStringLiteral("danger")), McpPermission::Off);
    QCOMPARE(config.permissionForTool(QStringLiteral("other")), McpPermission::Allow);
}

void TestMcpServerConfig::disabledServerForcesOff() {
    McpServerConfig config;
    config.enabled = false;
    config.defaultPermission = McpPermission::Allow;
    config.setPermissionForTool(QStringLiteral("tool"), McpPermission::Allow);
    QCOMPARE(config.permissionForTool(QStringLiteral("tool")), McpPermission::Off);
}

void TestMcpServerConfig::setPermissionForToolStoresOverride() {
    McpServerConfig config;
    config.setPermissionForTool(QStringLiteral("tool"), McpPermission::Allow);
    QCOMPARE(config.toolPermissions.value(QStringLiteral("tool")).toString(),
             QStringLiteral("allow"));
}

QTEST_GUILESS_MAIN(TestMcpServerConfig)
#include "tst_mcpserverconfig.moc"
