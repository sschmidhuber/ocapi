#include "core/backendconfig.h"
#include "core/backendregistry.h"
#include "core/chatsession.h"
#include "storage/chatdatabase.h"
#include "ui/composer.h"
#include "ui/mainwindow.h"
#include "ui/sidebar.h"
#include "ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace okapi;

namespace {

// Whether the sidebar tree still lists the session with the given id. (All
// MainWindow instances of the test process share one temp database, so
// counting items would include sessions of earlier tests; assert on the
// specific ids instead.)
bool sidebarHasSession(MainWindow &window, const QUuid &id) {
    auto *tree = window.findChild<QTreeWidget *>();
    if (!tree)
        return false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *group = tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            if (QUuid(group->child(j)->data(0, Qt::UserRole).toString()) == id)
                return true;
        }
    }
    return false;
}

// Clicks the given button of the modal confirmation dialog that
// onSessionDeleteRequested shows for multi-delete; the timer fires inside
// the dialog's nested event loop.
void clickModalButton(QMessageBox::StandardButton button) {
    QTimer::singleShot(0, [button]() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(box);
        auto *b = box->button(button);
        QVERIFY(b);
        b->click();
    });
}

} // namespace

// MainWindow writes okapi.conf/okapi.sqlite under the app config/data dirs;
// redirect them into a temp dir so the test does not touch real settings.
class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void newChatKeepsCurrentBackendAndModel();
    void switchingToNewBackendDoesNotLeakOldModel();
    void reconfiguredBackendDropsStaleDetectedModels();
    void streamErrorSurvivesLateFinished();
    void toolCallFailureAppendsToolMessageWithoutCrashing();
    void keyboardShortcuts();
    void reboundShortcutFiresImmediately();
    void newChatFocusesInput();
    void f9TogglesSidebar();
    void ctrlTabCyclesChats();
    void deleteSingleChatWithoutConfirmation();
    void deleteMultipleChatsAsksAndDeletesOnYes();
    void deleteMultipleChatsKeepsAllOnNo();
    void unreadChatIsNotDeleted();
    void statusBarShowsReadyAfterStreamFinishes();
    void followUpQueuedWhileStreaming();
    void editMessageTruncatesHistoryOnSend();
    void composerInsertTemplateAppendsTextAndAttachments();
    void promptMenuFiltersAndEnterConfirms();
    void composerFollowsDarkPalette();
    void promptMenuOpensAtCaretAnchor();
    void promptMenuArrowKeysNavigateVisibleEntries();
    void startupShowsNewChatAndLoadsOldChatsOnDemand();
    void recentChatsArePrefetchedInBackground();

private:
    QTemporaryDir m_home;
};

void TestMainWindow::initTestCase() {
    QVERIFY(m_home.isValid());
    // Redirect config and data into a per-process temp dir. Test mode is
    // deliberately NOT enabled: it forces the shared ~/.qttest paths and
    // would ignore these variables, letting sessions and backends leak
    // between test runs (and processes) and making order-dependent tests
    // flaky.
    qputenv("XDG_CONFIG_HOME", (m_home.path() + QStringLiteral("/.config")).toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + QStringLiteral("/.local/share")).toUtf8());
}

void TestMainWindow::newChatKeepsCurrentBackendAndModel() {
    MainWindow window;
    window.show();

    // Seed two backends: Mistral first (so the initial session lands on it),
    // Ollama second.
    auto &registry = window.registryForTest();
    auto &mistral = registry.add(QStringLiteral("Mistral AI"), BackendKind::Mistral);
    auto &ollama = registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
    registry.save();

    // The window created an initial session on startup; make sure it is the
    // first enabled backend, then simulate the user switching to Ollama and
    // picking one of its models.
    ChatSession *initial = window.currentSession();
    QVERIFY(initial);
    initial->setBackendId(ollama.id.toString());
    initial->setModel(QStringLiteral("llama3.2"));

    // "New chat" must continue with the same backend and model, not fall
    // back to the first backend (Mistral) or its persisted model.
    window.startNewChat();
    ChatSession *next = window.currentSession();
    QVERIFY(next);
    QVERIFY(next != initial);
    QCOMPARE(next->backendId(), ollama.id.toString());
    QCOMPARE(next->model(), QStringLiteral("llama3.2"));
    Q_UNUSED(mistral);
}

// Regression test: switching to a newly configured backend must not show the
// previous backend's model under the new backend. The session kept the old
// model while its backendId changed, and the picker then appended that
// foreign model to the new backend's model list.
void TestMainWindow::switchingToNewBackendDoesNotLeakOldModel() {
    MainWindow window;
    window.show();

    auto &registry = window.registryForTest();
    const QString ollamaId =
        registry.add(QStringLiteral("Ollama"), BackendKind::Ollama).id.toString();
    const QString freshId =
        registry.add(QStringLiteral("Fresh backend"), BackendKind::Ollama).id.toString();
    registry.save();

    ChatSession *session = window.currentSession();
    QVERIFY(session);
    session->setBackendId(ollamaId);
    session->setModel(QStringLiteral("llama3.2"));

    // Switch the chat to the new backend the way the picker menu does.
    auto *picker = window.modelPickerForTest();
    if (!picker)
        QFAIL("model picker not available");
    auto *menu = picker->menu();
    if (!menu)
        QFAIL("picker menu not built");
    QAction *freshAction = nullptr;
    for (auto *action : menu->actions()) {
        if (action->text() == QStringLiteral("Fresh backend"))
            freshAction = action;
    }
    if (!freshAction)
        QFAIL("backend action not found in picker menu");
    freshAction->trigger();

    // The session must no longer carry the old backend's model...
    QCOMPARE(session->backendId(), freshId);
    QVERIFY(session->model().isEmpty());
    // ...and the new backend's model list must not contain it, even after a
    // selector rebuild (e.g. when model detection for it completes).
    window.selectSessionForTest(session->id());
    const QStringList freshModels = picker->backendModels().value(freshId);
    QVERIFY(!freshModels.contains(QStringLiteral("llama3.2")));
    QVERIFY(picker->currentModel().isEmpty() || freshModels.contains(picker->currentModel()));
}

// Regression test: when a backend's configuration changes after its models
// were detected (e.g. a newly added backend is first probed with the default
// URL and only then edited), the stale model list must be dropped so the
// backend is queried again with the new config. Otherwise the picker keeps
// showing the previously configured backend's models until an app restart.
void TestMainWindow::reconfiguredBackendDropsStaleDetectedModels() {
    MainWindow window;
    window.show();

    auto &registry = window.registryForTest();
    registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
    const QString freshId =
        registry.add(QStringLiteral("New backend"), BackendKind::Ollama).id.toString();
    registry.save();
    auto *picker = window.modelPickerForTest();
    if (!picker)
        QFAIL("model picker not available");

    // A detection for the new backend completes (with whatever the backend
    // answered while it still had the default config).
    window.simulateModelsDetectedForTest(freshId, {QStringLiteral("stale-model")});
    QVERIFY(picker->backendModels().value(freshId).contains(QStringLiteral("stale-model")));

    // The user reconfigures the backend (different URL), as the settings page
    // signals via BackendRegistry::changed.
    auto *config = registry.find(QUuid(freshId));
    if (!config)
        QFAIL("backend not found in registry");
    config->baseUrl = QStringLiteral("http://127.0.0.1:59999");
    registry.save();
    emit registry.changed();

    // The stale list must be gone from the picker...
    QVERIFY(!picker->backendModels().value(freshId).contains(QStringLiteral("stale-model")));
    // ...and a fresh detection result replaces it.
    window.simulateModelsDetectedForTest(freshId, {QStringLiteral("fresh-model")});
    QCOMPARE(picker->backendModels().value(freshId), QStringList{QStringLiteral("fresh-model")});
}

// Regression test: when a backend reports failed() and then a late
// finished() for the same message (as SseClient emitted after an HTTP error),
// the error must stay visible in the chat instead of being cleared to an
// empty "Ready" turn.
void TestMainWindow::streamErrorSurvivesLateFinished() {
    MainWindow window;
    window.show();
    auto &registry = window.registryForTest();
    registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
    registry.save();
    window.startNewChat();
    ChatSession *session = window.currentSession();
    QVERIFY(session);

    window.simulateStreamErrorForTest(
        QStringLiteral("Your prepayment credits are depleted. See https://ai.google.dev/billing"),
        402);
    window.simulateLateStreamFinishForTest();
    // Let the deferred context deletion (singleShot(0)) run.
    QTest::qWait(20);

    bool checked = false;
    for (const auto &message : session->messages()) {
        if (message.role == Role::Assistant) {
            QVERIFY(message.error.contains(QStringLiteral("credits are depleted")));
            QVERIFY(!message.streaming);
            checked = true;
        }
    }
    QVERIFY(checked);
    // The status bar shows only the error class + HTTP status, not the full
    // server message (that goes to the error dialog).
    auto *statusBar = window.findChild<QStatusBar *>();
    QVERIFY(statusBar);
    QCOMPARE(statusBar->currentMessage(), QStringLiteral("Client error: HTTP 402"));
}

// Regression test: a synchronously failing MCP tool call (unknown tool, no
// server connected) appends a Role::Tool message from within the tool-call
// handling. The continuation must be deferred to the event loop so the
// backend object is never destroyed while its toolCallsRequested signal is
// still being delivered (that use-after-free crashed the app with
// "free(): invalid next size" / "invalid type id").
void TestMainWindow::toolCallFailureAppendsToolMessageWithoutCrashing() {
    MainWindow window;
    window.show();
    ChatSession *session = window.currentSession();
    QVERIFY(session);

    ToolCall call;
    call.id = QStringLiteral("call_1");
    call.name = QStringLiteral("no_such_server__no_such_tool");
    window.simulateToolCallsForTest({call});

    // The failure is reported synchronously as a tool message.
    bool sawToolMessage = false;
    for (const auto &message : session->messages()) {
        if (message.role == Role::Tool && message.toolCallId == call.id)
            sawToolMessage = true;
    }
    QVERIFY(sawToolMessage);

    // Let the deferred continuation run; must not crash or hang.
    QTest::qWait(100);
    QVERIFY(window.currentSession() == session);
}

// The toolbar/composer buttons must advertise their keyboard shortcuts in
// their tooltips, and Ctrl+N must start a new chat just like the button.
void TestMainWindow::keyboardShortcuts() {
    MainWindow window;
    window.show();

    QToolButton *newChatButton = nullptr;
    QToolButton *micButton = nullptr;
    for (auto *button : window.findChildren<QToolButton *>()) {
        if (button->text() == QStringLiteral("New"))
            newChatButton = button;
        if (button->accessibleName() == QStringLiteral("Dictate message"))
            micButton = button;
    }
    QVERIFY(newChatButton && newChatButton->toolTip().contains(QStringLiteral("Ctrl+N")));
    QVERIFY(micButton && micButton->toolTip().contains(QStringLiteral("Ctrl+.")));

    ChatSession *before = window.currentSession();
    QVERIFY(before);
    // Key events must reach the focus widget for window shortcuts to fire.
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QWidget *focusWidget = window.focusWidget();
    QVERIFY(focusWidget);
    QTest::keyClick(focusWidget, Qt::Key_N, Qt::ControlModifier);
    QVERIFY(window.currentSession());
    QVERIFY(window.currentSession() != before);
}

// Rebinding a shortcut in the registry must re-create the window's
// QShortcut immediately: after rebinding "New chat" to Ctrl+Shift+N, that
// combo starts a new chat without a restart.
void TestMainWindow::reboundShortcutFiresImmediately() {
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    ChatSession *before = window.currentSession();
    QVERIFY(before);
    window.shortcutsForTest().setSequence(QStringLiteral("newChat"),
                                          QKeySequence(QStringLiteral("Ctrl+Shift+N")));

    QWidget *focusWidget = window.focusWidget();
    QVERIFY(focusWidget);
    QTest::keyClick(focusWidget, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(window.currentSession());
    QVERIFY(window.currentSession() != before);
}

// F9 shows and hides the chat history sidebar.
void TestMainWindow::f9TogglesSidebar() {
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *sidebar = window.findChild<Sidebar *>();
    QVERIFY(sidebar);
    QVERIFY(sidebar->isVisible());

    QTest::keyClick(&window, Qt::Key_F9);
    QVERIFY(!sidebar->isVisible());

    QTest::keyClick(&window, Qt::Key_F9);
    QVERIFY(sidebar->isVisible());
}

// Ctrl+Tab / Ctrl+Shift+Tab switch to the next / previous chat of the
// history, wrapping around at both ends. Earlier tests of this binary
// persist their chats, so the window loads leftover sessions; the test
// places its own chats at the top of the history and cycles around the
// whole list instead of assuming it is alone.
void TestMainWindow::ctrlTabCyclesChats() {
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    ChatSession *oldest = window.currentSession();
    QVERIFY(oldest);
    window.startNewChat();
    ChatSession *middle = window.currentSession();
    window.startNewChat();
    ChatSession *newest = window.currentSession();
    QVERIFY(oldest != middle && middle != newest && oldest != newest);
    // Future timestamps put the three chats at the top of the newest-first
    // history, above any leftover chat of earlier tests.
    const QDateTime soon = QDateTime::currentDateTime().addSecs(3600);
    oldest->setLastActivityAt(soon.addSecs(-100));
    middle->setLastActivityAt(soon.addSecs(-50));
    newest->setLastActivityAt(soon);
    const int total = static_cast<int>(window.findChildren<ChatSession *>().size());
    QVERIFY(total >= 3);

    // Ctrl+Tab walks down the history: middle, oldest, then the leftovers,
    // and after a full cycle back to the newest.
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier);
    QVERIFY(window.currentSession() == middle);
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier);
    QVERIFY(window.currentSession() == oldest);
    for (int i = 0; i < total - 2; ++i)
        QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier);
    QVERIFY(window.currentSession() == newest);

    // Ctrl+Shift+Tab walks back up: one step wraps to the end of the list,
    // and a full cycle returns to the newest.
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(window.currentSession() != newest);
    for (int i = 0; i < total - 1; ++i)
        QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(window.currentSession() == newest);
    // Walking backward over the leftovers reaches our oldest chat again.
    for (int i = 0; i < total - 2; ++i)
        QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(window.currentSession() == oldest);
}

// Opening a new chat must move keyboard focus to the message input so the
// user can start typing immediately.
void TestMainWindow::newChatFocusesInput() {
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Move focus away from the input first to prove onNewChat restores it.
    QToolButton *settingsButton = nullptr;
    for (auto *button : window.findChildren<QToolButton *>()) {
        if (button->text() == QStringLiteral("Settings"))
            settingsButton = button;
    }
    if (!settingsButton) {
        QFAIL("Settings button not found");
        return;
    }
    settingsButton->setFocus();
    QCOMPARE(window.focusWidget(), settingsButton);

    window.startNewChat();
    QTest::qWait(50); // focus is applied via a deferred singleShot(0)
    QWidget *focus = window.focusWidget();
    QVERIFY(focus && focus->accessibleName() == QStringLiteral("Message input"));
}

// A single chat is deleted immediately, without a confirmation dialog (if
// one appeared, the test would hang in its event loop).
void TestMainWindow::deleteSingleChatWithoutConfirmation() {
    MainWindow window;
    window.show();
    const QUuid initial = window.currentSession()->id();
    window.startNewChat();
    const QUuid second = window.currentSession()->id();
    QVERIFY(sidebarHasSession(window, initial));
    QVERIFY(sidebarHasSession(window, second));

    window.requestDeleteSessionsForTest({second});
    QVERIFY(!sidebarHasSession(window, second));
    QVERIFY(sidebarHasSession(window, initial));
    QVERIFY(window.currentSession());
    QCOMPARE(window.currentSession()->id(), initial);
}

// Deleting several chats asks "Do you really want to delete x chats?" and
// removes them all when confirmed.
void TestMainWindow::deleteMultipleChatsAsksAndDeletesOnYes() {
    MainWindow window;
    window.show();
    const QUuid s0 = window.currentSession()->id();
    window.startNewChat();
    const QUuid s1 = window.currentSession()->id();
    window.startNewChat();
    const QUuid s2 = window.currentSession()->id();
    QVERIFY(sidebarHasSession(window, s0));
    QVERIFY(sidebarHasSession(window, s1));
    QVERIFY(sidebarHasSession(window, s2));

    clickModalButton(QMessageBox::Yes);
    window.requestDeleteSessionsForTest({s0, s1, s2});
    QVERIFY(!sidebarHasSession(window, s0));
    QVERIFY(!sidebarHasSession(window, s1));
    QVERIFY(!sidebarHasSession(window, s2));
    QVERIFY(window.currentSession());
    QVERIFY(window.currentSession()->id() != s0);
    QVERIFY(window.currentSession()->id() != s1);
    QVERIFY(window.currentSession()->id() != s2);
}

// Answering "No" to the multi-delete confirmation keeps all chats.
void TestMainWindow::deleteMultipleChatsKeepsAllOnNo() {
    MainWindow window;
    window.show();
    const QUuid s0 = window.currentSession()->id();
    window.startNewChat();
    const QUuid s1 = window.currentSession()->id();

    clickModalButton(QMessageBox::No);
    window.requestDeleteSessionsForTest({s0, s1});
    QVERIFY(sidebarHasSession(window, s0));
    QVERIFY(sidebarHasSession(window, s1));
}

// Unread chats are never deleted: the unread one is filtered out before the
// confirmation, so deleting {read, unread} is a single deletion (no dialog)
// and only removes the read one.
void TestMainWindow::unreadChatIsNotDeleted() {
    MainWindow window;
    window.show();
    const QUuid read = window.currentSession()->id();
    window.startNewChat();
    const QUuid unread = window.currentSession()->id();
    window.markUnreadForTest(unread);

    window.requestDeleteSessionsForTest({read, unread});
    QVERIFY(!sidebarHasSession(window, read));
    QVERIFY(sidebarHasSession(window, unread));
    QVERIFY(window.currentSession());
    QCOMPARE(window.currentSession()->id(), unread);
}

// Regression: after the last message of a turn finished, the status bar must
// show "Ready" again, not keep "Generating…" (the finished context still
// existed until its deferred deletion and updateStreamingUi() overwrote the
// final status with "Generating…").
void TestMainWindow::statusBarShowsReadyAfterStreamFinishes() {
    MainWindow window;
    window.show();
    auto *statusBar = window.findChild<QStatusBar *>();
    QVERIFY(statusBar);

    window.simulateStreamStartForTest();
    QVERIFY(statusBar->currentMessage().contains(QStringLiteral("Generating")));

    window.simulateStreamFinishForTest();
    // Let the deferred context deletion (singleShot(0)) run.
    QTest::qWait(20);
    QCOMPARE(statusBar->currentMessage(), QStringLiteral("Ready"));
}

// While a turn streams, the composer stays editable and a typed follow-up is
// queued; it is sent automatically once the current turn finishes.
void TestMainWindow::followUpQueuedWhileStreaming() {
    MainWindow window;
    window.show();
    auto &registry = window.registryForTest();
    auto &ollama = registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
    registry.save();
    ChatSession *session = window.currentSession();
    QVERIFY(session);
    session->setBackendId(ollama.id.toString());
    session->setModel(QStringLiteral("mock-model"));

    window.simulateStreamStartForTest();
    // Sending while the turn runs must queue the message, not send it.
    window.sendTextForTest(QStringLiteral("follow up question"));
    QVERIFY(session->messages().isEmpty());

    window.simulateStreamFinishForTest();
    // The deferred context deletion flushes the queue.
    QTest::qWait(100);
    bool sawFollowUp = false;
    for (const auto &message : session->messages()) {
        if (message.role == Role::User && message.content == QStringLiteral("follow up question"))
            sawFollowUp = true;
    }
    QVERIFY(sawFollowUp);
}

// Editing a user message and pressing Send removes the edited message and
// everything after it from the history, then sends the edited text.
void TestMainWindow::editMessageTruncatesHistoryOnSend() {
    MainWindow window;
    window.show();
    auto &registry = window.registryForTest();
    auto &ollama = registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
    registry.save();
    // Fresh chat: earlier tests of this process share the temp database, so
    // the initial session may already contain messages.
    window.startNewChat();
    ChatSession *session = window.currentSession();
    QVERIFY(session);
    session->setBackendId(ollama.id.toString());
    session->setModel(QStringLiteral("mock-model"));

    // Store the id by value: the messages vector reallocates on append.
    const QUuid userMsgId =
        session->appendMessage(Role::User, QStringLiteral("original question")).id;
    session->appendMessage(Role::Assistant, QStringLiteral("old answer"));
    session->appendMessage(Role::User, QStringLiteral("follow up"));
    QCOMPARE(session->messages().size(), 3);

    // Clicking the edit button loads the message into the composer.
    window.startEditMessageForTest(userMsgId);
    QVERIFY(window.isEditingMessageForTest());

    // Send the edited version: the old tail is gone (the remaining messages
    // are at most the system prompt, the new user message and the backend's
    // empty assistant placeholder).
    window.sendTextForTest(QStringLiteral("edited question"));
    QVERIFY(!window.isEditingMessageForTest());
    int userMessages = 0;
    for (const auto &message : session->messages()) {
        QVERIFY(!message.content.contains(QStringLiteral("old answer")));
        QVERIFY(!message.content.contains(QStringLiteral("follow up")));
        QVERIFY(!message.content.contains(QStringLiteral("original question")));
        if (message.role == Role::User) {
            ++userMessages;
            QCOMPARE(message.content, QStringLiteral("edited question"));
        }
    }
    QCOMPARE(userMessages, 1);
}

// insertTemplate (prompt-template autocompletion): inserts the template text
// at the cursor and appends its attachments.
void TestMainWindow::composerInsertTemplateAppendsTextAndAttachments() {
    MainWindow window;
    window.show();
    auto *composer = window.findChild<Composer *>();
    QVERIFY(composer);
    composer->setText(QStringLiteral("Hello "));
    ImageAttachment image;
    image.fileName = QStringLiteral("chart.png");
    image.mimeType = QStringLiteral("image/png");
    image.data = QByteArray("fakepng");
    composer->insertTemplate(QStringLiteral("Summarize this:"), {image}, {});
    QVERIFY(composer->text().contains(QStringLiteral("Summarize this:")));
    QCOMPARE(composer->attachments().size(), 1);
    QCOMPARE(composer->attachments().first().fileName, QStringLiteral("chart.png"));
}

// The prompt-template trigger: typing "/" opens the menu, typing on filters
// it ("/re" shows only "Recherche"), Enter confirms the top entry and replaces
// the typed trigger text with the template.
void TestMainWindow::promptMenuFiltersAndEnterConfirms() {
    MainWindow window;
    window.show();
    auto &reg = window.promptTemplatesForTest();
    PromptTemplate a;
    a.name = QStringLiteral("Analyse");
    a.text = QStringLiteral("Analysiere:");
    reg.add(a);
    PromptTemplate r;
    r.name = QStringLiteral("Recherche");
    r.text = QStringLiteral("Recherchiere:");
    reg.add(r);

    auto *composer = window.findChild<Composer *>();
    QVERIFY(composer);
    auto *edit = composer->findChild<QPlainTextEdit *>();
    QVERIFY(edit);
    edit->setFocus();

    QTest::keyClicks(edit, QStringLiteral("/re"));
    auto *menu = window.promptMenuForTest();
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    auto *list = menu->findChild<QListWidget *>();
    QVERIFY(list);
    int visible = 0;
    QString visibleName;
    for (int i = 0; i < list->count(); ++i) {
        if (!list->item(i)->isHidden()) {
            ++visible;
            visibleName = list->item(i)->text();
        }
    }
    QCOMPARE(visible, 1);
    QCOMPARE(visibleName, QStringLiteral("Recherche"));

    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(composer->text(), QStringLiteral("Recherchiere:"));
    QVERIFY(!menu->isVisible());
}

// The popup opens at the current caret position (IDE autocompletion style):
// horizontally aligned with the trigger character and directly below its
// line — not at the composer's top-left corner above the input.
void TestMainWindow::promptMenuOpensAtCaretAnchor() {
    MainWindow window;
    window.resize(400, 300);
    window.move(0, 0);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto &reg = window.promptTemplatesForTest();
    PromptTemplate a;
    a.name = QStringLiteral("Analyse");
    a.text = QStringLiteral("Analysiere:");
    reg.add(a);

    auto *composer = window.findChild<Composer *>();
    QVERIFY(composer);
    auto *edit = composer->findChild<QPlainTextEdit *>();
    QVERIFY(edit);
    edit->setFocus();

    // Second line, then the trigger: the caret (and anchor) is below the
    // composer's top edge, so the old "above the composer" placement and the
    // caret placement differ measurably.
    QTest::keyClicks(edit, QStringLiteral("hello"));
    QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(edit, QStringLiteral("/"));

    auto *menu = window.promptMenuForTest();
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    const QPoint anchor = composer->promptMenuAnchorGlobal();
    const QRect available = menu->screen()->availableGeometry();
    // Room below the caret in the offscreen screen: expect the popup right
    // below the trigger line, aligned with it horizontally.
    QVERIFY(anchor.y() + 2 + menu->height() <= available.bottom() + 1);
    QVERIFY(anchor.x() + menu->width() <= available.right() + 1);
    QCOMPARE(menu->pos().x(), anchor.x());
    QCOMPARE(menu->pos().y(), anchor.y() + 2);
    // And clearly below the composer's top edge (old behavior placed it
    // above the composer).
    QVERIFY(menu->pos().y() > composer->mapToGlobal(QPoint(0, 0)).y());
}

// Arrow up/down move the selection through the visible entries (wrapping at
// the ends, skipping filter-hidden ones) and are consumed, so the text caret
// does not move. Enter then confirms the selected entry.
void TestMainWindow::promptMenuArrowKeysNavigateVisibleEntries() {
    MainWindow window;
    window.show();
    auto &reg = window.promptTemplatesForTest();
    // Templates persist in the shared test settings across tests in this
    // process; start from a clean registry so the counts are deterministic.
    for (const auto &t : reg.templates())
        reg.remove(t.id);
    for (const auto &name :
         {QStringLiteral("Analyse"), QStringLiteral("Recherche"), QStringLiteral("Uebersetzen")}) {
        PromptTemplate t;
        t.name = name;
        t.text = name + QStringLiteral(":");
        reg.add(t);
    }

    auto *composer = window.findChild<Composer *>();
    QVERIFY(composer);
    auto *edit = composer->findChild<QPlainTextEdit *>();
    QVERIFY(edit);
    edit->setFocus();
    // The composer input is disabled while no backend is selected, which
    // would swallow the key events; enable it so the typed trigger/filter
    // actually lands in the edit.
    composer->setBusy(false);
    composer->setSendEnabled(true);
    edit->setEnabled(true);

    QTest::keyClicks(edit, QStringLiteral("/"));
    auto *menu = window.promptMenuForTest();
    QVERIFY(menu && menu->isVisible());
    auto *list = menu->findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->currentRow(), 0);

    QTest::keyClick(edit, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(edit, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 2);
    QTest::keyClick(edit, Qt::Key_Down); // wraps to the first entry
    QCOMPARE(list->currentRow(), 0);
    QTest::keyClick(edit, Qt::Key_Up); // wraps to the last entry
    QCOMPARE(list->currentRow(), 2);
    // The caret stayed right after the trigger character.
    QCOMPARE(edit->toPlainText(), QStringLiteral("/"));
    QCOMPARE(edit->textCursor().position(), 1);

    // With an active filter, hidden entries are skipped: "/re" leaves only
    // "Recherche" (row 1) visible, so navigation stays on it.
    QTest::keyClicks(edit, QStringLiteral("re"));
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(edit, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(edit, Qt::Key_Up);
    QCOMPARE(list->currentRow(), 1);

    // Enter confirms the selected entry.
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(composer->text(), QStringLiteral("Recherche:"));
    QVERIFY(!menu->isVisible());
}

// Seeds one stored chat with one message directly into the shared temp
// database (the same file MainWindow opens via the redirected XDG dirs).
static void seedStoredChat(const QUuid &id, const QString &title, const QString &content,
                           const QDateTime &activity) {
    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           QStringLiteral("/okapi.sqlite");
    ChatDatabase db;
    QVERIFY(db.open(dbPath));
    ChatSession seed(id, title, QStringLiteral("backend-seed"));
    seed.setCreatedAt(activity);
    seed.setLastActivityAt(activity);
    QVERIFY(db.insertSession(seed));
    QVERIFY(db.insertMessage(id.toString(QUuid::WithoutBraces),
                             QUuid::createUuid().toString(QUuid::WithoutBraces),
                             QStringLiteral("user"), content, activity));
}

// Startup shows a fresh "New chat" instead of reopening the last chat, and
// older chats are read from the database only once they are selected.
void TestMainWindow::startupShowsNewChatAndLoadsOldChatsOnDemand() {
    const QUuid oldId = QUuid::createUuid();
    const QDateTime oldTime = QDateTime::currentDateTime().addDays(-5);
    seedStoredChat(oldId, QStringLiteral("Old chat"), QStringLiteral("hello old"), oldTime);

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // The startup chat is a new one, not the stored chat.
    QVERIFY(window.currentSession());
    QCOMPARE(window.currentSession()->title(), QStringLiteral("New chat"));
    QVERIFY(window.currentSession()->id() != oldId);

    // The stored chat is listed but its messages are not loaded yet (it is
    // older than yesterday, so the background prefetch skips it).
    ChatSession *old = window.sessionForTest(oldId);
    QVERIFY(old);
    QVERIFY(!old->property("okapi_messagesLoaded").toBool());
    QVERIFY(old->messages().isEmpty());

    // Selecting it loads its newest page on demand.
    window.selectSessionForTest(oldId);
    QCOMPARE(window.currentSession(), old);
    QVERIFY(old->property("okapi_messagesLoaded").toBool());
    QCOMPARE(old->messages().size(), 1);
    QCOMPARE(old->messages().constFirst().content, QStringLiteral("hello old"));
}

// Chats of today (and yesterday) are prefetched in the background shortly
// after startup, so they open without a visible load delay.
void TestMainWindow::recentChatsArePrefetchedInBackground() {
    const QUuid recentId = QUuid::createUuid();
    // Within the last minutes: always "today" or "yesterday" at prefetch time.
    const QDateTime recentTime = QDateTime::currentDateTime().addSecs(-120);
    seedStoredChat(recentId, QStringLiteral("Recent chat"), QStringLiteral("hello recent"),
                   recentTime);

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    ChatSession *recent = window.sessionForTest(recentId);
    QVERIFY(recent);
    QTRY_VERIFY_WITH_TIMEOUT(recent->property("okapi_messagesLoaded").toBool(), 5000);
    QCOMPARE(recent->messages().size(), 1);
    QCOMPARE(recent->messages().constFirst().content, QStringLiteral("hello recent"));
    // Prefetching must not change which chat is shown: still the new chat.
    QCOMPARE(window.currentSession()->title(), QStringLiteral("New chat"));
}

int main(int argc, char *argv[]) {
    // MainWindow is a QWidget, so the test needs a QApplication; force the
    // offscreen platform like tst_chatview does so CI needs no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}

// In dark mode the composer's text edit must use the dark base color (its
// viewport follows the widget palette, not a stylesheet background).
void TestMainWindow::composerFollowsDarkPalette() {
    const QPalette original = qApp->palette();
    QVERIFY(applyTheme(UiSettings::Theme::Dark));
    Composer composer;
    composer.refreshTheme();
    auto *edit = composer.findChild<QPlainTextEdit *>();
    if (!edit)
        QFAIL("text edit not found");
    QVERIFY(edit->palette().color(QPalette::Base).lightness() < 128);
    QVERIFY(!applyTheme(UiSettings::Theme::Light));
    composer.refreshTheme();
    QVERIFY(edit->palette().color(QPalette::Base).lightness() >= 128);
    qApp->setPalette(original);
}

#include "tst_mainwindow.moc"
