#include "storage/chatdatabase.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestMcpServerDatabase : public QObject {
    Q_OBJECT

private slots:
    void init();
    void insertAndLoadServers();
    void insertOverwritesExistingServer();
    void deleteServerRemovesIt();
    void loadEmptyWhenNoServers();
    void multilineFieldsRoundTrip();
    void toolPermissionsJsonRoundTrips();

private:
    ChatDatabase &db() { return m_database; }
    QTemporaryDir m_tempDir;
    ChatDatabase m_database;
    QString m_filePath;
};

void TestMcpServerDatabase::init() {
    m_database.close();
    static int counter = 0;
    m_filePath = m_tempDir.filePath(QStringLiteral("ocapi-mcp-%1.sqlite").arg(counter++));
    QVERIFY(m_tempDir.isValid());
    QVERIFY2(db().open(m_filePath), db().lastError().toUtf8().constData());
}

void TestMcpServerDatabase::insertAndLoadServers() {
    McpServerConfig config;
    config.name = QStringLiteral("fs");
    config.transport = McpTransport::Stdio;
    config.command = QStringLiteral("npx");
    config.arguments = {QStringLiteral("-y"), QStringLiteral("server-fs")};
    config.environment = {QStringLiteral("A=1"), QStringLiteral("B=2")};
    config.headers = {QStringLiteral("X-Key: secret")};
    config.defaultPermission = McpPermission::Allow;
    config.enabled = true;
    db().insertMcpServer(config);

    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().id, config.id);
    QCOMPARE(loaded.first().name, QStringLiteral("fs"));
    QCOMPARE(loaded.first().transport, McpTransport::Stdio);
    QCOMPARE(loaded.first().command, QStringLiteral("npx"));
    QCOMPARE(loaded.first().arguments, config.arguments);
    QCOMPARE(loaded.first().environment, config.environment);
    QCOMPARE(loaded.first().headers, config.headers);
    QCOMPARE(loaded.first().defaultPermission, McpPermission::Allow);
    QVERIFY(loaded.first().enabled);
}

void TestMcpServerDatabase::insertOverwritesExistingServer() {
    McpServerConfig config;
    config.name = QStringLiteral("before");
    config.transport = McpTransport::StreamableHttp;
    config.url = QStringLiteral("http://localhost:1/mcp");
    db().insertMcpServer(config);

    config.name = QStringLiteral("after");
    config.url = QStringLiteral("http://localhost:2/mcp");
    config.enabled = false;
    db().insertMcpServer(config);

    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().name, QStringLiteral("after"));
    QCOMPARE(loaded.first().url, QStringLiteral("http://localhost:2/mcp"));
    QVERIFY(!loaded.first().enabled);
}

void TestMcpServerDatabase::deleteServerRemovesIt() {
    McpServerConfig a;
    a.name = QStringLiteral("a");
    McpServerConfig b;
    b.name = QStringLiteral("b");
    db().insertMcpServer(a);
    db().insertMcpServer(b);
    QCOMPARE(db().loadMcpServers().size(), 2);
    db().deleteMcpServer(a.id.toString(QUuid::WithoutBraces));
    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().name, QStringLiteral("b"));
}

void TestMcpServerDatabase::loadEmptyWhenNoServers() {
    QVERIFY(db().loadMcpServers().isEmpty());
}

void TestMcpServerDatabase::multilineFieldsRoundTrip() {
    McpServerConfig config;
    config.name = QStringLiteral("multi");
    config.transport = McpTransport::Stdio;
    config.command = QStringLiteral("run");
    config.environment = {QStringLiteral("A=1"), QString(), QStringLiteral("B=2")};
    config.headers = {QStringLiteral("X-One: 1"), QStringLiteral("X-Two: 2")};
    db().insertMcpServer(config);
    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    // Empty lines are dropped on save/load.
    QCOMPARE(loaded.first().environment,
             QStringList({QStringLiteral("A=1"), QStringLiteral("B=2")}));
    QCOMPARE(loaded.first().headers,
             QStringList({QStringLiteral("X-One: 1"), QStringLiteral("X-Two: 2")}));
}

void TestMcpServerDatabase::toolPermissionsJsonRoundTrips() {
    McpServerConfig config;
    config.name = QStringLiteral("perms");
    config.setPermissionForTool(QStringLiteral("read"), McpPermission::Allow);
    config.setPermissionForTool(QStringLiteral("write"), McpPermission::Off);
    db().insertMcpServer(config);
    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().permissionForTool(QStringLiteral("read")), McpPermission::Allow);
    QCOMPARE(loaded.first().permissionForTool(QStringLiteral("write")), McpPermission::Off);
    QCOMPARE(loaded.first().permissionForTool(QStringLiteral("other")), McpPermission::Ask);
}

QTEST_GUILESS_MAIN(TestMcpServerDatabase)
#include "tst_mcpserverdatabase.moc"
