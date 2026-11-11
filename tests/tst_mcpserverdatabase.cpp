#include "storage/chatdatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

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
    void apiKeyRoundTrips();
    void migratesLegacySchemaWithoutApiKey();

private:
    ChatDatabase &db() { return m_database; }
    QTemporaryDir m_tempDir;
    ChatDatabase m_database;
    QString m_filePath;
};

void TestMcpServerDatabase::init() {
    m_database.close();
    static int counter = 0;
    m_filePath = m_tempDir.filePath(QStringLiteral("okapi-mcp-%1.sqlite").arg(counter++));
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
    config.apiKey = QStringLiteral("secret-token");
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
    QCOMPARE(loaded.first().apiKey, QStringLiteral("secret-token"));
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

void TestMcpServerDatabase::apiKeyRoundTrips() {
    McpServerConfig config;
    config.name = QStringLiteral("http-with-key");
    config.transport = McpTransport::StreamableHttp;
    config.url = QStringLiteral("http://127.0.0.1:3000/mcp");
    config.apiKey = QStringLiteral("Bearer-less-token");
    db().insertMcpServer(config);
    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().apiKey, QStringLiteral("Bearer-less-token"));

    // Overwriting the server updates the key as well.
    config.apiKey = QStringLiteral("rotated");
    db().insertMcpServer(config);
    const auto reloaded = db().loadMcpServers();
    QCOMPARE(reloaded.size(), 1);
    QCOMPARE(reloaded.first().apiKey, QStringLiteral("rotated"));
}

void TestMcpServerDatabase::migratesLegacySchemaWithoutApiKey() {
    db().close();
    // Build a database with the pre-api_key schema (no api_key column).
    const QString legacyPath = m_tempDir.filePath(QStringLiteral("legacy-mcp.sqlite"));
    {
        QSqlDatabase raw =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy-mcp"));
        raw.setDatabaseName(legacyPath);
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE mcp_servers ("
            "id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, "
            "transport TEXT NOT NULL DEFAULT 'http', url TEXT NOT NULL DEFAULT '', "
            "command TEXT NOT NULL DEFAULT '', arguments TEXT NOT NULL DEFAULT '', "
            "environment TEXT NOT NULL DEFAULT '', headers TEXT NOT NULL DEFAULT '', "
            "default_permission TEXT NOT NULL DEFAULT 'ask', "
            "tool_permissions TEXT NOT NULL DEFAULT '{}', enabled INTEGER NOT NULL DEFAULT 1)")));
        QVERIFY(q.exec(
            QStringLiteral("INSERT INTO mcp_servers (id, name) VALUES ('legacy-1', 'legacy')")));
        raw.close();
        QSqlDatabase::removeDatabase(QStringLiteral("legacy-mcp"));
    }
    // Opening through ChatDatabase runs the migration.
    QVERIFY2(db().open(legacyPath), db().lastError().toUtf8().constData());
    const auto loaded = db().loadMcpServers();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().name, QStringLiteral("legacy"));
    QVERIFY(loaded.first().apiKey.isEmpty());
    // Writes after the migration persist the key.
    McpServerConfig config;
    config.name = QStringLiteral("post-migration");
    config.apiKey = QStringLiteral("secret");
    db().insertMcpServer(config);
    const auto reloaded = db().loadMcpServers();
    QCOMPARE(reloaded.size(), 2);
    bool found = false;
    for (const auto &server : reloaded) {
        if (server.name == QLatin1String("post-migration")) {
            found = true;
            QCOMPARE(server.apiKey, QStringLiteral("secret"));
        }
    }
    QVERIFY(found);
}

QTEST_GUILESS_MAIN(TestMcpServerDatabase)
#include "tst_mcpserverdatabase.moc"
