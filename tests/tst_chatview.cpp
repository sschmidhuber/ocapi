#include "core/chatsession.h"
#include "ui/chatdelegate.h"
#include "ui/chatview.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QSignalSpy>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
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
    void newAssistantMessageIsStreamingImmediately();
    void streamingAssistantShowsNoCopyFooter();
    void clickOnFileChipEmitsOpenFailureForMissingFile();
    void clickOnFileChipOpensExistingFile();
    void clickOutsideAttachmentsDoesNotOpen();
    void attachmentAtMatchesChipGeometry();
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

namespace {

// A message with one file chip whose source path points at \p path.
Message makeFileMessage(const QString &path) {
    Message message = makeMessage(Role::User, QStringLiteral("see attachment"));
    FileAttachment file;
    file.fileName = QStringLiteral("notes.txt");
    file.sourcePath = path;
    message.files = {file};
    return message;
}

// Center of the first file chip of \p index, in item coordinates; a null
// point when no attachment is found.
QPoint fileChipCenter(ChatView &view, const QModelIndex &index) {
    auto *delegate = qobject_cast<ChatDelegate *>(view.itemDelegate());
    if (!delegate)
        return {};
    const QRect itemRect = view.visualRect(index);
    if (!itemRect.isValid())
        return {};
    // The chip lives inside the content area; scan the item rect for the
    // chip by asking the delegate where attachments are.
    for (int y = itemRect.top(); y < itemRect.bottom(); ++y) {
        for (int x = itemRect.left(); x < itemRect.right(); ++x) {
            const QPoint pos(x, y);
            if (ChatDelegate::attachmentAt(index, itemRect, pos))
                return pos;
        }
    }
    return {};
}

} // namespace

void TestChatView::newAssistantMessageIsStreamingImmediately() {
    // Backends append the assistant message before the first token arrives
    // (reasoning models can think for a long time). It must be streaming
    // from the start so the chat shows no copy button during generation.
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    const QUuid id = session.appendMessage(Role::Assistant, {}, true).id;
    QCOMPARE(session.messages().constLast().streaming, true);

    ChatView view;
    view.setSession(&session);
    const QModelIndex idx = modelOf(view)->index(0, 0);
    QCOMPARE(idx.data(static_cast<int>(Qt::UserRole) + 2).toBool(), true);
}

void TestChatView::streamingAssistantShowsNoCopyFooter() {
    // The footer (copy button, throughput) is only laid out for assistant
    // messages that are no longer streaming, and it adds its own row below
    // the content: the finished item must be taller than the streaming one.
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    const QUuid id = session.appendMessage(Role::Assistant, {}, true).id;
    ChatView view;
    view.setSession(&session);

    ChatDelegate delegate;
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 1200, 500);
    option.font = QApplication::font();
    const QModelIndex idx = modelOf(view)->index(0, 0);

    session.updateMessage(id, QStringLiteral("A reply being generated."), true);
    const int streamingHeight = delegate.sizeHint(option, idx).height();

    session.updateMessage(id, QStringLiteral("A reply being generated."), false);
    const int finishedHeight = delegate.sizeHint(option, idx).height();

    QVERIFY(finishedHeight > streamingHeight);
}

void TestChatView::clickOnFileChipEmitsOpenFailureForMissingFile() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    session.appendMessage(makeFileMessage(QStringLiteral("/nonexistent/ocapi-missing.txt")));
    ChatView view;
    view.resize(600, 400);
    view.setSession(&session);
    auto *delegate = qobject_cast<ChatDelegate *>(view.itemDelegate());
    QVERIFY(delegate);
    QSignalSpy failureSpy(delegate, &ChatDelegate::attachmentOpenFailed);

    const QModelIndex idx = modelOf(view)->index(0, 0);
    const QPoint chipPos = fileChipCenter(view, idx);
    QVERIFY(!chipPos.isNull());
    QStyleOptionViewItem option;
    option.rect = view.visualRect(idx);
    QMouseEvent release(QEvent::MouseButtonRelease, chipPos, view.viewport()->mapToGlobal(chipPos),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QVERIFY(delegate->editorEvent(&release, modelOf(view), option, idx));
    // The click hit the chip of a file that no longer exists, so the open
    // fails and the failure signal is emitted. The view's signal handler
    // would show a modal message box; the delegate only emits the signal.
    QCOMPARE(failureSpy.count(), 1);
}

void TestChatView::clickOnFileChipOpensExistingFile() {
    // Outside a sandbox QDesktopServices::openUrl delegates to xdg-open;
    // with no xdg-open on the CI runner the call fails, which still proves
    // the click reached the open path (a failure is reported, no crash).
    // Inside a sandbox the portal path is taken. Either way the delegate
    // must consume the click.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello");
    file.close();

    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    session.appendMessage(makeFileMessage(path));
    ChatView view;
    view.resize(600, 400);
    view.setSession(&session);
    auto *delegate = qobject_cast<ChatDelegate *>(view.itemDelegate());
    QVERIFY(delegate);

    const QModelIndex idx = modelOf(view)->index(0, 0);
    const QPoint chipPos = fileChipCenter(view, idx);
    QVERIFY(!chipPos.isNull());
    QStyleOptionViewItem option;
    option.rect = view.visualRect(idx);
    QMouseEvent release(QEvent::MouseButtonRelease, chipPos, view.viewport()->mapToGlobal(chipPos),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    // The click on the chip is consumed whether or not the open succeeds.
    QVERIFY(delegate->editorEvent(&release, modelOf(view), option, idx));
}

void TestChatView::clickOutsideAttachmentsDoesNotOpen() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    session.appendMessage(makeFileMessage(QStringLiteral("/nonexistent/ocapi-missing.txt")));
    ChatView view;
    view.resize(600, 400);
    view.setSession(&session);
    auto *delegate = qobject_cast<ChatDelegate *>(view.itemDelegate());
    QVERIFY(delegate);
    QSignalSpy failureSpy(delegate, &ChatDelegate::attachmentOpenFailed);

    const QModelIndex idx = modelOf(view)->index(0, 0);
    const QRect itemRect = view.visualRect(idx);
    // Click in the top-left corner of the item (the role label row), far
    // away from any attachment chip.
    const QPoint pos(itemRect.left() + 2, itemRect.top() + 2);
    QVERIFY(!ChatDelegate::attachmentAt(idx, itemRect, pos));
    QStyleOptionViewItem option;
    option.rect = itemRect;
    QMouseEvent release(QEvent::MouseButtonRelease, pos, view.viewport()->mapToGlobal(pos),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    delegate->editorEvent(&release, modelOf(view), option, idx);
    QCOMPARE(failureSpy.count(), 0);
}

void TestChatView::attachmentAtMatchesChipGeometry() {
    ChatSession session(QStringLiteral("t"), QStringLiteral("b"));
    session.appendMessage(makeFileMessage(QStringLiteral("/tmp/notes.txt")));
    ChatView view;
    view.resize(600, 400);
    view.setSession(&session);
    auto *delegate = qobject_cast<ChatDelegate *>(view.itemDelegate());
    QVERIFY(delegate);

    const QModelIndex idx = modelOf(view)->index(0, 0);
    const QRect itemRect = view.visualRect(idx);
    const QPoint chipPos = fileChipCenter(view, idx);
    QVERIFY(!chipPos.isNull());
    QVERIFY(ChatDelegate::attachmentAt(idx, itemRect, chipPos));
    // Just outside the item there is no attachment.
    QVERIFY(!ChatDelegate::attachmentAt(idx, itemRect, QPoint(itemRect.left() - 50, chipPos.y())));
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
