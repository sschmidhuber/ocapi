#include "storage/chatdatabase.h"

#include "core/chatsession.h"
#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestChatDatabase : public QObject {
    Q_OBJECT

private slots:
    void init();
    void openCreatesTables();
    void settingsRoundTrip();
    void settingsOverwriteExistingKey();
    void settingsMissingKeyReturnsDefault();
    void insertAndListSessions();
    void sessionMetadataRoundTrip();
    void insertAndLoadMessages();
    void messageUpdateClearsError();
    void failMessagePersistsError();
    void deleteSessionRemovesMessages();
    void updateSessionChangesMetadata();
    void backendRoundTrip();
    void backendUpdateOverwritesEntry();
    void backendDeleteRemovesEntry();
    void messagePaginationReturnsLastPage();
    void messagePaginationSecondPageIsOlderMessages();
    void messagePaginationBeyondEndIsEmpty();
    void messageCountMatchesStoredMessages();

private:
    ChatDatabase &db() { return m_database; }
    QTemporaryDir m_tempDir;
    ChatDatabase m_database;
    QString m_filePath;
};

void TestChatDatabase::init() {
    m_database.close();
    static int counter = 0;
    m_filePath = m_tempDir.filePath(QStringLiteral("ocapi-%1.sqlite").arg(counter++));
    QVERIFY(m_tempDir.isValid());
    QVERIFY2(db().open(m_filePath), db().lastError().toUtf8().constData());
}

void TestChatDatabase::openCreatesTables() {
    QVERIFY(db().isOpen());
    QVERIFY(QFile::exists(m_filePath));
}

void TestChatDatabase::settingsRoundTrip() {
    QVERIFY(db().setValue(QStringLiteral("ui/theme"), QStringLiteral("breeze")));
    QCOMPARE(db().value(QStringLiteral("ui/theme")), QStringLiteral("breeze"));
}

void TestChatDatabase::settingsOverwriteExistingKey() {
    QVERIFY(db().setValue(QStringLiteral("key"), QStringLiteral("a")));
    QVERIFY(db().setValue(QStringLiteral("key"), QStringLiteral("b")));
    QCOMPARE(db().value(QStringLiteral("key")), QStringLiteral("b"));
}

void TestChatDatabase::settingsMissingKeyReturnsDefault() {
    QCOMPARE(db().value(QStringLiteral("missing"), QStringLiteral("fallback")),
             QStringLiteral("fallback"));
}

void TestChatDatabase::insertAndListSessions() {
    ChatSession first(QStringLiteral("First"), QStringLiteral("backend-1"));
    ChatSession second(QStringLiteral("Second"), QStringLiteral("backend-2"));
    QVERIFY(db().insertSession(first));
    QVERIFY(db().insertSession(second));
    const auto ids = db().sessionIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(first.id().toString(QUuid::WithoutBraces)));
    QVERIFY(ids.contains(second.id().toString(QUuid::WithoutBraces)));
}

void TestChatDatabase::sessionMetadataRoundTrip() {
    ChatSession session(QStringLiteral("My chat"), QStringLiteral("backend-1"));
    session.setModel(QStringLiteral("llama3"));
    QVERIFY(db().insertSession(session));
    const auto id = session.id().toString(QUuid::WithoutBraces);
    QCOMPARE(db().sessionTitle(id), QStringLiteral("My chat"));
    QCOMPARE(db().sessionBackendId(id), QStringLiteral("backend-1"));
    QCOMPARE(db().sessionModel(id), QStringLiteral("llama3"));
}

void TestChatDatabase::insertAndLoadMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto userMsg = session.appendMessage(Role::User, QStringLiteral("hello"));
    const auto assistantMsg = session.appendMessage(Role::Assistant, QStringLiteral("hi"));
    QVERIFY(db().insertMessage(sessionId, userMsg.id.toString(QUuid::WithoutBraces),
                               userMsg.roleName(), userMsg.content, userMsg.timestamp));
    QVERIFY(db().insertMessage(sessionId, assistantMsg.id.toString(QUuid::WithoutBraces),
                               assistantMsg.roleName(), assistantMsg.content,
                               assistantMsg.timestamp));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).role, QStringLiteral("user"));
    QCOMPARE(stored.at(0).content, QStringLiteral("hello"));
    QCOMPARE(stored.at(1).role, QStringLiteral("assistant"));
    QCOMPARE(stored.at(1).content, QStringLiteral("hi"));
}

void TestChatDatabase::messageUpdateClearsError() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QStringLiteral("partial"));
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY2(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                                message.timestamp),
             db().lastError().toUtf8().constData());
    QVERIFY(db().failMessage(messageId, QStringLiteral("boom")));
    QVERIFY(db().updateMessage(messageId, QStringLiteral("full text"), false));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).content, QStringLiteral("full text"));
    QVERIFY(!stored.at(0).streaming);
    QVERIFY(stored.at(0).error.isEmpty());
}

void TestChatDatabase::failMessagePersistsError() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto message = session.appendMessage(Role::Assistant, QStringLiteral("..."));
    const auto messageId = message.id.toString(QUuid::WithoutBraces);
    QVERIFY2(db().insertMessage(sessionId, messageId, message.roleName(), message.content,
                                message.timestamp),
             db().lastError().toUtf8().constData());
    QVERIFY(db().failMessage(messageId, QStringLiteral("connection refused")));
    const auto stored = db().messagesForSession(sessionId);
    QCOMPARE(stored.at(0).error, QStringLiteral("connection refused"));
    QVERIFY(!stored.at(0).streaming);
}

void TestChatDatabase::deleteSessionRemovesMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    const auto message = session.appendMessage(Role::User, QStringLiteral("hi"));
    QVERIFY(db().insertSession(session));
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp));
    QVERIFY(db().deleteSession(sessionId));
    QVERIFY(db().sessionIds().isEmpty());
    QVERIFY(db().messagesForSession(sessionId).isEmpty());
}

void TestChatDatabase::updateSessionChangesMetadata() {
    ChatSession session(QStringLiteral("Old title"), QStringLiteral("backend-1"));
    QVERIFY(db().insertSession(session));
    session.setTitle(QStringLiteral("New title"));
    session.setModel(QStringLiteral("mistral"));
    QVERIFY(db().updateSession(session));
    const auto id = session.id().toString(QUuid::WithoutBraces);
    QCOMPARE(db().sessionTitle(id), QStringLiteral("New title"));
    QCOMPARE(db().sessionModel(id), QStringLiteral("mistral"));
}

void TestChatDatabase::backendRoundTrip() {
    BackendConfig config;
    config.name = QStringLiteral("Ollama local");
    config.kind = BackendKind::Ollama;
    config.baseUrl = QStringLiteral("http://192.168.1.10:11434");
    config.apiKey = QStringLiteral("secret");
    config.defaultModel = QStringLiteral("llama3.2");
    config.enabled = false;
    db().insertBackend(config);
    QVERIFY(!db().loadBackends().isEmpty());
    const auto loaded = db().loadBackends();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("Ollama local"));
    QCOMPARE(loaded.at(0).kind, BackendKind::Ollama);
    QCOMPARE(loaded.at(0).baseUrl, QStringLiteral("http://192.168.1.10:11434"));
    QCOMPARE(loaded.at(0).apiKey, QStringLiteral("secret"));
    QCOMPARE(loaded.at(0).defaultModel, QStringLiteral("llama3.2"));
    QVERIFY(!loaded.at(0).enabled);
}

void TestChatDatabase::backendUpdateOverwritesEntry() {
    BackendConfig config;
    config.name = QStringLiteral("before");
    config.kind = BackendKind::OpenAi;
    db().insertBackend(config);
    config.name = QStringLiteral("after");
    config.kind = BackendKind::Groq;
    config.enabled = false;
    db().updateBackend(config);
    const auto loaded = db().loadBackends();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("after"));
    QCOMPARE(loaded.at(0).kind, BackendKind::Groq);
    QVERIFY(!loaded.at(0).enabled);
}

void TestChatDatabase::backendDeleteRemovesEntry() {
    BackendConfig config;
    config.name = QStringLiteral("doomed");
    db().insertBackend(config);
    QVERIFY(!db().loadBackends().isEmpty());
    db().deleteBackend(config.id.toString(QUuid::WithoutBraces));
    QVERIFY(db().loadBackends().isEmpty());
    QVERIFY(db().loadBackends().isEmpty());
}

void TestChatDatabase::messagePaginationReturnsLastPage() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    for (int i = 0; i < 5; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    const auto page = db().messagesForSession(sessionId, 2, 0);
    QCOMPARE(page.size(), 2);
    QCOMPARE(page.at(0).content, QStringLiteral("m3"));
    QCOMPARE(page.at(1).content, QStringLiteral("m4"));
}

void TestChatDatabase::messagePaginationSecondPageIsOlderMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    for (int i = 0; i < 5; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    const auto page = db().messagesForSession(sessionId, 2, 2);
    QCOMPARE(page.size(), 2);
    QCOMPARE(page.at(0).content, QStringLiteral("m1"));
    QCOMPARE(page.at(1).content, QStringLiteral("m2"));
}

void TestChatDatabase::messagePaginationBeyondEndIsEmpty() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    const auto &message = session.appendMessage(Role::User, QStringLiteral("only"));
    QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                               message.roleName(), message.content, message.timestamp));
    QVERIFY(db().messagesForSession(sessionId, 10, 5).isEmpty());
}

void TestChatDatabase::messageCountMatchesStoredMessages() {
    ChatSession session(QStringLiteral("Chat"), QStringLiteral("backend-1"));
    const auto sessionId = session.id().toString(QUuid::WithoutBraces);
    QVERIFY(db().insertSession(session));
    QCOMPARE(db().messageCount(sessionId), 0);
    for (int i = 0; i < 3; ++i) {
        const auto &message = session.appendMessage(Role::User, QStringLiteral("m%1").arg(i));
        QVERIFY(db().insertMessage(sessionId, message.id.toString(QUuid::WithoutBraces),
                                   message.roleName(), message.content, message.timestamp));
    }
    QCOMPARE(db().messageCount(sessionId), 3);
}

QTEST_GUILESS_MAIN(TestChatDatabase)
#include "tst_chatdatabase.moc"
