#include "core/chatsession.h"

#include <QHash>
#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

class TestChatSession : public QObject {
    Q_OBJECT

private slots:
    void defaultConstructorUsesNewChatTitle();
    void parameterizedConstructorSetsTitleAndBackend();
    void appendMessageAddsMessageAndEmitsSignal();
    void appendMessageReturnsReferenceToStoredMessage();
    void updateMessageModifiesContentAndStreamingFlag();
    void updateMessageClearsError();
    void updateMessageIgnoresUnknownId();
    void failMessageSetsErrorAndStopsStreaming();
    void failMessageIgnoresUnknownId();
    void setTitleEmitsOnlyOnChange();
    void clearRemovesAllMessages();
    void prependMessagesSplicesInOlderHistory();
    void prependMessagesWithAlreadyPresentIdsDuplicatesIds();
    void sessionsHaveUniqueIds();
    void setThroughputUpdatesMessageAndEmitsSignal();
    void setThroughputIgnoresUnknownId();
    void setThroughputIgnoresSameValue();
    void timestampsAreInitialized();
    void appendMessageUpdatesLastActivity();
    void setTimestampsAcceptValidValues();
};

void TestChatSession::defaultConstructorUsesNewChatTitle() {
    ChatSession session;
    QVERIFY(!session.title().isEmpty());
    QVERIFY(session.backendId().isEmpty());
    QVERIFY(session.model().isEmpty());
    QVERIFY(session.messages().isEmpty());
}

void TestChatSession::parameterizedConstructorSetsTitleAndBackend() {
    ChatSession session;
    session.setTitle(QStringLiteral("My chat"));
    session.setBackendId(QStringLiteral("backend-1"));
    QCOMPARE(session.title(), QStringLiteral("My chat"));
    QCOMPARE(session.backendId(), QStringLiteral("backend-1"));
}

void TestChatSession::appendMessageAddsMessageAndEmitsSignal() {
    ChatSession session;
    QSignalSpy addedSpy(&session, &ChatSession::messageAdded);
    auto &message = session.appendMessage(Role::User, QStringLiteral("hello"));
    QCOMPARE(session.messages().size(), 1);
    QCOMPARE(message.role, Role::User);
    QCOMPARE(message.content, QStringLiteral("hello"));
    QCOMPARE(addedSpy.count(), 1);
    const auto emitted = addedSpy.takeFirst().at(0).value<ocapi::Message>();
    QCOMPARE(emitted.id, message.id);
}

void TestChatSession::appendMessageReturnsReferenceToStoredMessage() {
    ChatSession session;
    auto &message = session.appendMessage(Role::Assistant);
    message.content = QStringLiteral("edited");
    QCOMPARE(session.messages().constLast().content, QStringLiteral("edited"));
}

void TestChatSession::updateMessageModifiesContentAndStreamingFlag() {
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);

    session.updateMessage(id, QStringLiteral("partial"), true);
    const auto &message = session.messages().constLast();
    QCOMPARE(message.content, QStringLiteral("partial"));
    QVERIFY(message.streaming);
    QCOMPARE(updatedSpy.count(), 1);

    session.updateMessage(id, QStringLiteral("full text"), false);
    QCOMPARE(session.messages().constLast().content, QStringLiteral("full text"));
    QVERIFY(!session.messages().constLast().streaming);
    QCOMPARE(updatedSpy.count(), 2);
}

void TestChatSession::updateMessageClearsError() {
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    session.failMessage(id, QStringLiteral("boom"));
    QVERIFY(!session.messages().constLast().error.isEmpty());

    session.updateMessage(id, QStringLiteral("recovered"), false);
    QVERIFY(session.messages().constLast().error.isEmpty());
}

void TestChatSession::updateMessageIgnoresUnknownId() {
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.updateMessage(QUuid::createUuid(), QStringLiteral("x"), false);
    QCOMPARE(session.messages().constLast().content, QStringLiteral("hello"));
    QCOMPARE(updatedSpy.count(), 0);
}

void TestChatSession::failMessageSetsErrorAndStopsStreaming() {
    ChatSession session;
    auto &message = session.appendMessage(Role::Assistant);
    message.streaming = true;
    const QUuid id = message.id;
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);

    session.failMessage(id, QStringLiteral("connection refused"));
    const auto &failed = session.messages().constLast();
    QCOMPARE(failed.error, QStringLiteral("connection refused"));
    QVERIFY(!failed.streaming);
    QCOMPARE(updatedSpy.count(), 1);
}

void TestChatSession::failMessageIgnoresUnknownId() {
    ChatSession session;
    session.appendMessage(Role::User);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.failMessage(QUuid::createUuid(), QStringLiteral("boom"));
    QCOMPARE(updatedSpy.count(), 0);
    QVERIFY(session.messages().constLast().error.isEmpty());
}

void TestChatSession::setTitleEmitsOnlyOnChange() {
    ChatSession session;
    session.setTitle(QStringLiteral("initial"));
    QSignalSpy titleSpy(&session, &ChatSession::titleChanged);

    session.setTitle(QStringLiteral("initial"));
    QCOMPARE(titleSpy.count(), 0);

    session.setTitle(QStringLiteral("renamed"));
    QCOMPARE(session.title(), QStringLiteral("renamed"));
    QCOMPARE(titleSpy.count(), 1);
    QCOMPARE(titleSpy.takeFirst().at(0).toString(), QStringLiteral("renamed"));
}

void TestChatSession::clearRemovesAllMessages() {
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("one"));
    session.appendMessage(Role::Assistant, QStringLiteral("two"));
    QCOMPARE(session.messages().size(), 2);
    session.clear();
    QVERIFY(session.messages().isEmpty());
}

void TestChatSession::prependMessagesSplicesInOlderHistory() {
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("new question"));
    session.appendMessage(Role::Assistant, QStringLiteral("new answer"));
    QSignalSpy addedSpy(&session, &ChatSession::messageAdded);

    QVector<Message> older;
    Message oldMessage;
    oldMessage.role = Role::User;
    oldMessage.content = QStringLiteral("old question");
    older.push_back(oldMessage);

    session.prependMessages(older);
    QCOMPARE(session.messages().size(), 3);
    QCOMPARE(session.messages().first().content, QStringLiteral("old question"));
    QCOMPARE(session.messages().last().content, QStringLiteral("new answer"));
    // prependMessages must not emit messageAdded: the messages are already
    // persisted and re-emitting would re-persist them.
    QCOMPARE(addedSpy.count(), 0);
}

void TestChatSession::prependMessagesWithAlreadyPresentIdsDuplicatesIds() {
    // Guards the duplication bug from issue #48: callers must not prepend
    // messages the session already holds. The session itself trusts its
    // caller, so this test documents the contract: after prepending the
    // session's own messages, ids appear twice (the caller-side guard in
    // MainWindow::loadOlderMessages prevents this in practice).
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("q"));
    session.appendMessage(Role::Assistant, QStringLiteral("a"));
    const QVector<Message> copy = session.messages();
    session.prependMessages(copy);
    QCOMPARE(session.messages().size(), 4);
    QHash<QUuid, int> idCount;
    for (const auto &m : session.messages())
        idCount.insert(m.id, idCount.value(m.id) + 1);
    for (auto it = idCount.constBegin(); it != idCount.constEnd(); ++it)
        QCOMPARE(it.value(), 2);
    QCOMPARE(session.messages().at(0).id, session.messages().at(2).id);
    QCOMPARE(session.messages().at(1).id, session.messages().at(3).id);
}

void TestChatSession::sessionsHaveUniqueIds() {
    ChatSession a;
    ChatSession b;
    QVERIFY(!a.id().isNull());
    QVERIFY(a.id() != b.id());
}

void TestChatSession::setThroughputUpdatesMessageAndEmitsSignal() {
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.setThroughput(id, 42.5);
    QCOMPARE(session.messages().constLast().tokensPerSecond, 42.5);
    QCOMPARE(updatedSpy.count(), 1);
}

void TestChatSession::setThroughputIgnoresUnknownId() {
    ChatSession session;
    session.appendMessage(Role::Assistant);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.setThroughput(QUuid::createUuid(), 10.0);
    QCOMPARE(updatedSpy.count(), 0);
    QCOMPARE(session.messages().constLast().tokensPerSecond, -1.0);
}

void TestChatSession::setThroughputIgnoresSameValue() {
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    session.setThroughput(id, 30.0);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.setThroughput(id, 30.0);
    QCOMPARE(updatedSpy.count(), 0);
    QCOMPARE(session.messages().constLast().tokensPerSecond, 30.0);
}

void TestChatSession::timestampsAreInitialized() {
    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    ChatSession session;
    const QDateTime after = QDateTime::currentDateTime().addSecs(1);
    QVERIFY(session.createdAt().isValid());
    QVERIFY(session.lastActivityAt().isValid());
    QVERIFY(session.createdAt() >= before && session.createdAt() <= after);
    QVERIFY(session.lastActivityAt() >= before && session.lastActivityAt() <= after);
}

void TestChatSession::appendMessageUpdatesLastActivity() {
    ChatSession session;
    const QDateTime past(QDate(2020, 1, 1), QTime(0, 0));
    session.setLastActivityAt(past);
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QVERIFY(session.lastActivityAt() > past);
}

void TestChatSession::setTimestampsAcceptValidValues() {
    ChatSession session;
    const QDateTime created(QDate(2024, 5, 1), QTime(10, 0));
    const QDateTime activity(QDate(2024, 5, 2), QTime(11, 0));
    session.setCreatedAt(created);
    session.setLastActivityAt(activity);
    QCOMPARE(session.createdAt(), created);
    QCOMPARE(session.lastActivityAt(), activity);
    // Invalid values are ignored.
    session.setCreatedAt(QDateTime());
    session.setLastActivityAt(QDateTime());
    QCOMPARE(session.createdAt(), created);
    QCOMPARE(session.lastActivityAt(), activity);
}

QTEST_GUILESS_MAIN(TestChatSession)
#include "tst_chatsession.moc"
