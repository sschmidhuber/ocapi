#include "core/mcpregistry.h"

#include "storage/chatdatabase.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestMcpRegistry : public QObject {
    Q_OBJECT

private slots:
    void addAppendsServerWithDefaults();
    void addEmitsChanged();
    void removeExistingServerReturnsTrue();
    void removeUnknownServerReturnsFalse();
    void findReturnsMatchingServer();
    void enabledFiltersDisabledServers();
    void permissionForToolUsesConfigLogic();
    void setPermissionForToolPersistsAndEmits();
    void databaseRoundTripPreservesAllFields();
    void removePersistsToDatabase();
};

void TestMcpRegistry::addAppendsServerWithDefaults() {
    McpRegistry registry;
    auto &config = registry.add(QStringLiteral("fs"), McpTransport::Stdio);
    QCOMPARE(registry.servers().size(), 1);
    QCOMPARE(config.name, QStringLiteral("fs"));
    QCOMPARE(config.transport, McpTransport::Stdio);
    QVERIFY(config.enabled);
    QVERIFY(!config.id.isNull());
    QCOMPARE(config.defaultPermission, McpPermission::Ask);
}

void TestMcpRegistry::addEmitsChanged() {
    McpRegistry registry;
    QSignalSpy spy(&registry, &McpRegistry::changed);
    registry.add(QStringLiteral("a"), McpTransport::StreamableHttp);
    QCOMPARE(spy.count(), 1);
}

void TestMcpRegistry::removeExistingServerReturnsTrue() {
    McpRegistry registry;
    auto &config = registry.add(QStringLiteral("to remove"), McpTransport::Stdio);
    QSignalSpy spy(&registry, &McpRegistry::changed);
    QVERIFY(registry.remove(config.id));
    QVERIFY(registry.servers().isEmpty());
    QCOMPARE(spy.count(), 1);
}

void TestMcpRegistry::removeUnknownServerReturnsFalse() {
    McpRegistry registry;
    registry.add(QStringLiteral("stay"), McpTransport::StreamableHttp);
    QSignalSpy spy(&registry, &McpRegistry::changed);
    QVERIFY(!registry.remove(QUuid::createUuid()));
    QCOMPARE(registry.servers().size(), 1);
    QCOMPARE(spy.count(), 0);
}

void TestMcpRegistry::findReturnsMatchingServer() {
    McpRegistry registry;
    auto &added = registry.add(QStringLiteral("find me"), McpTransport::Stdio);
    McpServerConfig *found = registry.find(added.id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->name, QStringLiteral("find me"));
    QVERIFY(registry.find(QUuid::createUuid()) == nullptr);
}

void TestMcpRegistry::enabledFiltersDisabledServers() {
    McpRegistry registry;
    registry.add(QStringLiteral("on"), McpTransport::StreamableHttp);
    auto &off = registry.add(QStringLiteral("off"), McpTransport::Stdio);
    off.enabled = false;
    const auto result = registry.enabled();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first().name, QStringLiteral("on"));
}

void TestMcpRegistry::permissionForToolUsesConfigLogic() {
    McpRegistry registry;
    auto &config = registry.add(QStringLiteral("srv"), McpTransport::StreamableHttp);
    config.defaultPermission = McpPermission::Allow;
    QCOMPARE(registry.permissionForTool(config.id, QStringLiteral("tool")), McpPermission::Allow);
    QCOMPARE(registry.permissionForTool(QUuid::createUuid(), QStringLiteral("tool")),
             McpPermission::Off);
}

void TestMcpRegistry::setPermissionForToolPersistsAndEmits() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ChatDatabase database;
    QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
    McpRegistry registry;
    registry.attachDatabase(&database);
    auto &config = registry.add(QStringLiteral("srv"), McpTransport::StreamableHttp);
    QSignalSpy spy(&registry, &McpRegistry::changed);
    registry.setPermissionForTool(config.id, QStringLiteral("tool"), McpPermission::Allow);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(config.permissionForTool(QStringLiteral("tool")), McpPermission::Allow);

    McpRegistry reloaded;
    reloaded.load(database);
    QCOMPARE(reloaded.servers().size(), 1);
    QCOMPARE(reloaded.servers().first().permissionForTool(QStringLiteral("tool")),
             McpPermission::Allow);
}

void TestMcpRegistry::databaseRoundTripPreservesAllFields() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        ChatDatabase database;
        QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
        McpRegistry registry;
        registry.attachDatabase(&database);
        auto &a = registry.add(QStringLiteral("Filesystem"), McpTransport::Stdio);
        a.command = QStringLiteral("npx");
        a.arguments = {QStringLiteral("-y"), QStringLiteral("@modelcontextprotocol/server-fs")};
        a.environment = {QStringLiteral("NODE_ENV=production")};
        a.defaultPermission = McpPermission::Allow;
        a.setPermissionForTool(QStringLiteral("write_file"), McpPermission::Off);
        registry.save();
        auto &b = registry.add(QStringLiteral("Remote"), McpTransport::StreamableHttp);
        b.url = QStringLiteral("https://mcp.example.com/mcp");
        b.headers = {QStringLiteral("Authorization: Bearer token")};
        b.enabled = false;
        registry.save();
    }
    {
        ChatDatabase database;
        QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
        McpRegistry registry;
        registry.load(database);
        QCOMPARE(registry.servers().size(), 2);

        const auto *fs = registry.find(QStringLiteral("Filesystem"));
        QVERIFY(fs == nullptr); // find takes an id, not a name
        McpServerConfig *stdio = nullptr;
        McpServerConfig *http = nullptr;
        for (auto &config : registry.servers()) {
            if (config.transport == McpTransport::Stdio)
                stdio = &config;
            else
                http = &config;
        }
        QVERIFY(stdio != nullptr);
        QCOMPARE(stdio->name, QStringLiteral("Filesystem"));
        QCOMPARE(stdio->command, QStringLiteral("npx"));
        const QStringList expectedArgs{
            QStringLiteral("-y"),
            QStringLiteral("@modelcontextprotocol/server-fs"),
        };
        QCOMPARE(stdio->arguments, expectedArgs);
        QCOMPARE(stdio->environment, QStringList({QStringLiteral("NODE_ENV=production")}));
        QCOMPARE(stdio->defaultPermission, McpPermission::Allow);
        QCOMPARE(stdio->permissionForTool(QStringLiteral("write_file")), McpPermission::Off);
        QVERIFY(stdio->enabled);

        QVERIFY(http != nullptr);
        QCOMPARE(http->name, QStringLiteral("Remote"));
        QCOMPARE(http->url, QStringLiteral("https://mcp.example.com/mcp"));
        QCOMPARE(http->headers, QStringList({QStringLiteral("Authorization: Bearer token")}));
        QVERIFY(!http->enabled);
        Q_UNUSED(fs)
    }
}

void TestMcpRegistry::removePersistsToDatabase() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ChatDatabase database;
    QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
    McpRegistry registry;
    registry.attachDatabase(&database);
    auto &config = registry.add(QStringLiteral("temp"), McpTransport::Stdio);
    QVERIFY(registry.remove(config.id));
    McpRegistry reloaded;
    reloaded.load(database);
    QVERIFY(reloaded.servers().isEmpty());
}

QTEST_GUILESS_MAIN(TestMcpRegistry)
#include "tst_mcpregistry.moc"
