#include "core/chatsession.h"

#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

class TestChatSessionTools : public QObject {
    Q_OBJECT

private slots:
    void setToolCallsAttachesCallsAndEmitsUpdate();
    void setToolCallsIgnoresUnknownId();
    void setToolCallsIgnoresIdenticalCalls();
    void toolCallEqualityComparesAllFields();
    void messageIsEmptyAccountsForToolCalls();
};

void TestChatSessionTools::setToolCallsAttachesCallsAndEmitsUpdate() {
    ChatSession session;
    auto &message = session.appendMessage(Role::Assistant);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    QVector<ToolCall> calls;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__tool");
    call.arguments = QJsonObject{{QStringLiteral("k"), QStringLiteral("v")}};
    calls.push_back(call);
    session.setToolCalls(message.id, calls);
    QCOMPARE(updatedSpy.count(), 1);
    QCOMPARE(session.messages().first().toolCalls.size(), 1);
    QCOMPARE(session.messages().first().toolCalls.first().name, QStringLiteral("srv__tool"));
    QVERIFY(!session.messages().first().streaming);
}

void TestChatSessionTools::setToolCallsIgnoresUnknownId() {
    ChatSession session;
    session.appendMessage(Role::Assistant);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    QVector<ToolCall> calls;
    ToolCall call;
    call.name = QStringLiteral("x");
    calls.push_back(call);
    session.setToolCalls(QUuid::createUuid(), calls);
    QCOMPARE(updatedSpy.count(), 0);
    QVERIFY(session.messages().first().toolCalls.isEmpty());
}

void TestChatSessionTools::setToolCallsIgnoresIdenticalCalls() {
    ChatSession session;
    auto &message = session.appendMessage(Role::Assistant);
    QVector<ToolCall> calls;
    ToolCall call;
    call.id = QStringLiteral("call-1");
    call.name = QStringLiteral("srv__tool");
    calls.push_back(call);
    session.setToolCalls(message.id, calls);
    QSignalSpy updatedSpy(&session, &ChatSession::messageUpdated);
    session.setToolCalls(message.id, calls);
    QCOMPARE(updatedSpy.count(), 0);
}

void TestChatSessionTools::toolCallEqualityComparesAllFields() {
    ToolCall a;
    a.id = QStringLiteral("1");
    a.name = QStringLiteral("n");
    a.arguments = QJsonObject{{QStringLiteral("k"), 1}};
    ToolCall b = a;
    QVERIFY(a == b);
    b.id = QStringLiteral("2");
    QVERIFY(!(a == b));
    b = a;
    b.name = QStringLiteral("other");
    QVERIFY(!(a == b));
    b = a;
    b.arguments = QJsonObject{{QStringLiteral("k"), 2}};
    QVERIFY(!(a == b));
}

void TestChatSessionTools::messageIsEmptyAccountsForToolCalls() {
    Message message;
    QVERIFY(message.isEmpty());
    ToolCall call;
    call.name = QStringLiteral("n");
    message.toolCalls.push_back(call);
    QVERIFY(!message.isEmpty());
    QVERIFY(message.hasToolCalls());
}

QTEST_GUILESS_MAIN(TestChatSessionTools)
#include "tst_chatsession_tools.moc"
