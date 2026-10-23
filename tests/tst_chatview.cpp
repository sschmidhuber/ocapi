#include "core/chatsession.h"
#include "ui/chatview.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QtTest>

using namespace ocapi;

namespace {

QAbstractItemModel *modelOf(ChatView &view) {
    return view.model();
}

Message makeMessage(Role role, const QString &content) {
    Message message;
    message.role = role;
    message.content = content;
    return message;
}

ToolCall makeCall(const QString &name, ToolCallStatus status, const QString &result = {}) {
    ToolCall call;
    call.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    call.name = name;
    call.status = status;
    call.result = result;
    return call;
}

} // namespace

class TestChatView : public QObject {
    Q_OBJECT

private slots:
    void accessibleTextIsMessageContent();
    void accessibleTextSpellsOutToolCallsWithStatus();
    void accessibleTextStripsServerPrefix();
    void accessibleTextAppendsToolResult();
    void accessibleTextCombinesToolCallsAndContent();
    void accessibleTextUpdatesWhenToolCallFinishes();
    void accessibleTextFailedHasFailedWord();
};

void TestChatView::accessibleTextIsMessageContent() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    session.appendMessage(makeMessage(Role::Assistant, QStringLiteral("Hello world")));
    ChatView view;
    view.setSession(&session);
    const QModelIndex idx = modelOf(view)->index(0, 0);
    QCOMPARE(idx.data(Qt::AccessibleTextRole).toString(), QStringLiteral("Hello world"));
}

void TestChatView::accessibleTextSpellsOutToolCallsWithStatus() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, {});
    message.toolCalls = {makeCall(QStringLiteral("server__echo"), ToolCallStatus::Done)};
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QString text = modelOf(view)->index(0, 0).data(Qt::AccessibleTextRole).toString();
    QVERIFY(text.contains(QStringLiteral("echo")));
    QVERIFY(text.contains(QStringLiteral("done")));
}

void TestChatView::accessibleTextStripsServerPrefix() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, {});
    message.toolCalls = {makeCall(QStringLiteral("myserver__mytool"), ToolCallStatus::Done)};
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QString text = modelOf(view)->index(0, 0).data(Qt::AccessibleTextRole).toString();
    QVERIFY(text.contains(QStringLiteral("mytool")));
    QVERIFY(!text.contains(QStringLiteral("myserver")));
}

void TestChatView::accessibleTextAppendsToolResult() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, {});
    message.toolCalls = {
        makeCall(QStringLiteral("echo"), ToolCallStatus::Done, QStringLiteral("tool output here")),
    };
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QString text = modelOf(view)->index(0, 0).data(Qt::AccessibleTextRole).toString();
    QVERIFY(text.contains(QStringLiteral("tool output here")));
}

void TestChatView::accessibleTextCombinesToolCallsAndContent() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, QStringLiteral("Final answer"));
    message.toolCalls = {makeCall(QStringLiteral("echo"), ToolCallStatus::Done)};
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QString text = modelOf(view)->index(0, 0).data(Qt::AccessibleTextRole).toString();
    QVERIFY(text.contains(QStringLiteral("echo")));
    QVERIFY(text.contains(QStringLiteral("Final answer")));
}

void TestChatView::accessibleTextUpdatesWhenToolCallFinishes() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, {});
    const ToolCall call = makeCall(QStringLiteral("server__echo"), ToolCallStatus::Running);
    message.toolCalls = {call};
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QModelIndex idx = modelOf(view)->index(0, 0);
    QVERIFY(idx.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("running")));

    session.setToolCallStatus(message.id, call.id, ToolCallStatus::Done, QStringLiteral("echoed"));
    QVERIFY(idx.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("done")));
    QVERIFY(idx.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("echoed")));
    QVERIFY(!idx.data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("running")));
}

void TestChatView::accessibleTextFailedHasFailedWord() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    Message message = makeMessage(Role::Assistant, {});
    message.toolCalls = {makeCall(QStringLiteral("echo"), ToolCallStatus::Failed)};
    session.appendMessage(message);
    ChatView view;
    view.setSession(&session);
    const QString text = modelOf(view)->index(0, 0).data(Qt::AccessibleTextRole).toString();
    QVERIFY(text.contains(QStringLiteral("failed")));
    QVERIFY(!text.contains(QStringLiteral("done")));
}

// ChatView is a QWidget, so the test needs a QApplication; force the
// offscreen platform because the CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestChatView test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_chatview.moc"
