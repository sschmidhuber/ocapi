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

QTEST_GUILESS_MAIN(TestChatDatabase)
#include "tst_chatdatabase.moc"
