#include "storage/chatdatabase.h"

#include "core/chatsession.h"

#include <algorithm>

#include <QAtomicInt>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace ocapi {

namespace {

constexpr auto kConnectionName = "ocapi_chat";

int nextConnectionCounter() {
    static QAtomicInt counter = 0;
    return counter.fetchAndAddRelaxed(1);
}

void createTables(QSqlDatabase &database) {
    QSqlQuery query(database);
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS backends ("
                              "id TEXT PRIMARY KEY NOT NULL, "
                              "name TEXT NOT NULL, "
                              "kind TEXT NOT NULL, "
                              "base_url TEXT NOT NULL DEFAULT '', "
                              "api_key TEXT NOT NULL DEFAULT '', "
                              "default_model TEXT NOT NULL DEFAULT '', "
                              "enabled INTEGER NOT NULL DEFAULT 1)"));
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS settings ("
                              "key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)"));
    query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sessions ("
                              "id TEXT PRIMARY KEY NOT NULL, "
                              "title TEXT NOT NULL, "
                              "backend_id TEXT NOT NULL, "
                              "model TEXT, "
                              "created_at INTEGER NOT NULL)"));
    query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS messages ("
                       "id TEXT PRIMARY KEY NOT NULL, "
                       "session_id TEXT NOT NULL, "
                       "role TEXT NOT NULL, "
                       "content TEXT NOT NULL, "
                       "streaming INTEGER NOT NULL DEFAULT 0, "
                       "error TEXT NOT NULL DEFAULT '', "
                       "timestamp INTEGER NOT NULL, "
                       "position INTEGER NOT NULL, "
                       "FOREIGN KEY (session_id) REFERENCES sessions (id) ON DELETE CASCADE)"));
}

} // namespace

ChatDatabase::ChatDatabase()
    : m_connectionName(QString::fromLatin1(kConnectionName) + QLatin1Char('-') +
                       QString::number(nextConnectionCounter())) {}

ChatDatabase::~ChatDatabase() {
    close();
}

bool ChatDatabase::open(const QString &filePath) {
    close();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(filePath);
    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        return false;
    }
    QSqlQuery pragma(m_database);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    createTables(m_database);
    if (m_database.lastError().isValid() && m_database.lastError().type() != QSqlError::NoError)
        m_lastError = m_database.lastError().text();
    return true;
}

void ChatDatabase::close() {
    if (!m_database.isValid())
        return;
    if (m_database.isOpen())
        m_database.close();
    m_lastError.clear();
    const QString name = m_connectionName;
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool ChatDatabase::execute(const QString &query, const QVariantList &bindings) {
    QSqlQuery statement(m_database);
    statement.prepare(query);
    for (const auto &binding : bindings)
        statement.addBindValue(binding);
    if (!statement.exec()) {
        m_lastError = statement.lastError().text();
        return false;
    }
    return true;
}

bool ChatDatabase::setValue(const QString &key, const QString &value) {
    return execute(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                                  "ON CONFLICT (key) DO UPDATE SET value = excluded.value"),
                   {key, value});
}

QString ChatDatabase::value(const QString &key, const QString &defaultValue) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return defaultValue;
    return query.value(0).toString();
}

namespace {

QVariant nullToEmpty(const QString &value) {
    return value.isEmpty() ? QVariant(QStringLiteral("")) : QVariant(value);
}

StoredMessage storedMessageFrom(const QSqlQuery &query) {
    StoredMessage message;
    message.id = query.value(0).toString();
    message.role = query.value(1).toString();
    message.content = query.value(2).toString();
    message.streaming = query.value(3).toInt() != 0;
    message.error = query.value(4).toString();
    message.timestamp = QDateTime::fromSecsSinceEpoch(query.value(5).toLongLong());
    return message;
}

} // namespace

QStringList ChatDatabase::sessionIds() const {
    QStringList ids;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id FROM sessions ORDER BY created_at ASC"))) {
        m_lastError = query.lastError().text();
        return ids;
    }
    while (query.next())
        ids.push_back(query.value(0).toString());
    return ids;
}

bool ChatDatabase::insertSession(const ChatSession &session) {
    return execute(QStringLiteral("INSERT INTO sessions (id, title, backend_id, model, "
                                  "created_at) VALUES (?, ?, ?, ?, ?) "
                                  "ON CONFLICT (id) DO UPDATE SET title = excluded.title, "
                                  "backend_id = excluded.backend_id, model = excluded.model"),
                   {
                       session.id().toString(QUuid::WithoutBraces),
                       session.title(),
                       session.backendId(),
                       session.model(),
                       QDateTime::currentDateTime().toSecsSinceEpoch(),
                   });
}

bool ChatDatabase::updateSession(const ChatSession &session) {
    return execute(QStringLiteral("UPDATE sessions SET title = ?, backend_id = ?, model = ? "
                                  "WHERE id = ?"),
                   {
                       session.title(),
                       session.backendId(),
                       session.model(),
                       session.id().toString(QUuid::WithoutBraces),
                   });
}

bool ChatDatabase::deleteSession(const QString &sessionId) {
    execute(QStringLiteral("DELETE FROM messages WHERE session_id = ?"), {sessionId});
    return execute(QStringLiteral("DELETE FROM sessions WHERE id = ?"), {sessionId});
}

int ChatDatabase::messageCount(const QString &sessionId) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM messages WHERE session_id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return 0;
    }
    return query.value(0).toInt();
}

void ChatDatabase::insertBackend(const BackendConfig &config) {
    execute(QStringLiteral("INSERT INTO backends (id, name, kind, base_url, api_key, "
                           "default_model, enabled) VALUES (?, ?, ?, ?, ?, ?, ?) "
                           "ON CONFLICT (id) DO UPDATE SET name = excluded.name, "
                           "kind = excluded.kind, base_url = excluded.base_url, "
                           "api_key = excluded.api_key, "
                           "default_model = excluded.default_model, "
                           "enabled = excluded.enabled"),
            {
                config.id.toString(QUuid::WithoutBraces),
                nullToEmpty(config.name),
                BackendConfig::kindKey(config.kind),
                nullToEmpty(config.baseUrl),
                nullToEmpty(config.apiKey),
                nullToEmpty(config.defaultModel),
                config.enabled ? 1 : 0,
            });
}

void ChatDatabase::updateBackend(const BackendConfig &config) {
    insertBackend(config);
}

void ChatDatabase::deleteBackend(const QString &backendId) {
    execute(QStringLiteral("DELETE FROM backends WHERE id = ?"), {backendId});
}

QVector<BackendConfig> ChatDatabase::loadBackends() const {
    QVector<BackendConfig> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id, name, kind, base_url, api_key, default_model, "
                                   "enabled FROM backends ORDER BY rowid ASC"))) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        BackendConfig config;
        config.id = QUuid(query.value(0).toString());
        if (config.id.isNull())
            config.id = QUuid::createUuid();
        config.name = query.value(1).toString();
        config.kind = BackendConfig::kindFromString(query.value(2).toString());
        config.baseUrl = query.value(3).toString();
        if (config.baseUrl.isEmpty())
            config.baseUrl = BackendConfig::defaultBaseUrl(config.kind);
        config.apiKey = query.value(4).toString();
        config.defaultModel = query.value(5).toString();
        config.enabled = query.value(6).toInt() != 0;
        result.push_back(std::move(config));
    }
    return result;
}

QVector<StoredMessage> ChatDatabase::messagesForSession(const QString &sessionId, int limit,
                                                        int offset) const {
    QVector<StoredMessage> result;
    QSqlQuery query(m_database);
    if (limit >= 0) {
        query.prepare(QStringLiteral("SELECT id, role, content, streaming, error, timestamp "
                                     "FROM messages WHERE session_id = ? "
                                     "ORDER BY position DESC LIMIT ? OFFSET ?"));
        query.addBindValue(sessionId);
        query.addBindValue(limit);
        query.addBindValue(offset);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return result;
        }
        while (query.next())
            result.push_back(storedMessageFrom(query));
        std::reverse(result.begin(), result.end());
        return result;
    }
    query.prepare(QStringLiteral("SELECT id, role, content, streaming, error, timestamp "
                                 "FROM messages WHERE session_id = ? ORDER BY position ASC"));
    query.addBindValue(sessionId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next())
        result.push_back(storedMessageFrom(query));
    return result;
}

bool ChatDatabase::insertMessage(const QString &sessionId, const QString &messageId,
                                 const QString &role, const QString &content,
                                 const QDateTime &timestamp) {
    QSqlQuery position(m_database);
    position.prepare(QStringLiteral("SELECT COALESCE(MAX(position) + 1, 0) FROM messages "
                                    "WHERE session_id = ?"));
    position.addBindValue(sessionId);
    if (!position.exec() || !position.next()) {
        m_lastError = position.lastError().text();
        return false;
    }
    return execute(QStringLiteral("INSERT INTO messages (id, session_id, role, content, "
                                  "streaming, error, timestamp, position) "
                                  "VALUES (?, ?, ?, ?, 0, '', ?, ?) "
                                  "ON CONFLICT (id) DO UPDATE SET role = excluded.role, "
                                  "content = excluded.content"),
                   {
                       messageId,
                       sessionId,
                       role,
                       content,
                       timestamp.isValid() ? timestamp.toSecsSinceEpoch() : 0,
                       position.value(0),
                   });
}

bool ChatDatabase::updateMessage(const QString &messageId, const QString &content, bool streaming) {
    return execute(QStringLiteral("UPDATE messages SET content = ?, streaming = ?, "
                                  "error = '' WHERE id = ?"),
                   {content, streaming ? 1 : 0, messageId});
}

bool ChatDatabase::failMessage(const QString &messageId, const QString &error) {
    return execute(QStringLiteral("UPDATE messages SET streaming = 0, error = ? WHERE id = ?"),
                   {error, messageId});
}

QString ChatDatabase::sessionTitle(const QString &sessionId) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT title FROM sessions WHERE id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return {};
    }
    return query.value(0).toString();
}

QString ChatDatabase::sessionBackendId(const QString &sessionId) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT backend_id FROM sessions WHERE id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return {};
    }
    return query.value(0).toString();
}

QString ChatDatabase::sessionModel(const QString &sessionId) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT model FROM sessions WHERE id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text();
        return {};
    }
    return query.value(0).toString();
}

} // namespace ocapi
