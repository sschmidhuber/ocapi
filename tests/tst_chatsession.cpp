#include "core/chatsession.h"

#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

class TestChatSession : public QObject
{
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
    void sessionsHaveUniqueIds();
};

void TestChatSession::defaultConstructorUsesNewChatTitle()
{
    ChatSession session;
    QVERIFY(!session.title().isEmpty());
    QVERIFY(session.backendId().isEmpty());
    QVERIFY(session.model().isEmpty());
    QVERIFY(session.messages().isEmpty());
}

void TestChatSession::parameterizedConstructorSetsTitleAndBackend()
{
    ChatSession session;
    session.setTitle(QStringLiteral("My chat"));
    session.setBackendId(QStringLiteral("backend-1"));
    QCOMPARE(session.title(), QStringLiteral("My chat"));
    QCOMPARE(session.backendId(), QStringLiteral("backend-1"));
}

void TestChatSession::appendMessageAddsMessageAndEmitsSignal()
{
    ChatSession session;
    QSignalSpy addedSpy(&session, &ChatSession::messageAdded);
    auto& message = session.appendMessage(Role::User, QStringLiteral("hello"));
    QCOMPARE(session.messages().size(), 1);
    QCOMPARE(message.role, Role::User);
    QCOMPARE(message.content, QStringLiteral("hello"));
    QCOMPARE(addedSpy.count(), 1);
    const auto emitted = addedSpy.takeFirst().at(0).value<ocapi::Message>();
    QCOMPARE(emitted.id, message.id);
}

void TestChatSession::appendMessageReturnsReferenceToStoredMessage()
{
    ChatSession session;
    auto& message = session.appendMessage(Role::Assistant);
    message.content = QStringLiteral("edited");
    QCOMPARE(session.messages().constLast().content, QStringLiteral("edited"));
}

void TestChatSession::updateMessageModifiesContentAndStreamingFlag()
{
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);

    session.updateMessage(id, QStringLiteral("partial"), true);
    const auto& message = session.messages().constLast();
    QCOMPARE(message.content, QStringLiteral("partial"));
    QVERIFY(message.streaming);
    QCOMPARE(updatedSpy.count(), 1);

    session.updateMessage(id, QStringLiteral("full text"), false);
    QCOMPARE(session.messages().constLast().content, QStringLiteral("full text"));
    QVERIFY(!session.messages().constLast().streaming);
    QCOMPARE(updatedSpy.count(), 2);
}

void TestChatSession::updateMessageClearsError()
{
    ChatSession session;
    const QUuid id = session.appendMessage(Role::Assistant).id;
    session.failMessage(id, QStringLiteral("boom"));
    QVERIFY(!session.messages().constLast().error.isEmpty());

    session.updateMessage(id, QStringLiteral("recovered"), false);
    QVERIFY(session.messages().constLast().error.isEmpty());
}

void TestChatSession::updateMessageIgnoresUnknownId()
{
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("hello"));
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.updateMessage(QUuid::createUuid(), QStringLiteral("x"), false);
    QCOMPARE(session.messages().constLast().content, QStringLiteral("hello"));
    QCOMPARE(updatedSpy.count(), 0);
}

void TestChatSession::failMessageSetsErrorAndStopsStreaming()
{
    ChatSession session;
    auto& message = session.appendMessage(Role::Assistant);
    message.streaming = true;
    const QUuid id = message.id;
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);

    session.failMessage(id, QStringLiteral("connection refused"));
    const auto& failed = session.messages().constLast();
    QCOMPARE(failed.error, QStringLiteral("connection refused"));
    QVERIFY(!failed.streaming);
    QCOMPARE(updatedSpy.count(), 1);
}

void TestChatSession::failMessageIgnoresUnknownId()
{
    ChatSession session;
    session.appendMessage(Role::User);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.failMessage(QUuid::createUuid(), QStringLiteral("boom"));
    QCOMPARE(updatedSpy.count(), 0);
    QVERIFY(session.messages().constLast().error.isEmpty());
}

void TestChatSession::setTitleEmitsOnlyOnChange()
{
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

void TestChatSession::clearRemovesAllMessages()
{
    ChatSession session;
    session.appendMessage(Role::User, QStringLiteral("one"));
    session.appendMessage(Role::Assistant, QStringLiteral("two"));
    QCOMPARE(session.messages().size(), 2);
    session.clear();
    QVERIFY(session.messages().isEmpty());
}

void TestChatSession::sessionsHaveUniqueIds()
{
    ChatSession a;
    ChatSession b;
    QVERIFY(!a.id().isNull());
    QVERIFY(a.id() != b.id());
}

QTEST_GUILESS_MAIN(TestChatSession)
#include "tst_chatsession.moc"
