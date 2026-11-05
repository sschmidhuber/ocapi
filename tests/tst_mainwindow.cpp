#include "core/backendconfig.h"
#include "core/backendregistry.h"
#include "core/chatsession.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace ocapi;

// MainWindow writes ocapi.conf/ocapi.sqlite under the app config/data dirs;
// redirect them into a temp dir so the test does not touch real settings.
class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void newChatKeepsCurrentBackendAndModel();
    void toolCallFailureAppendsToolMessageWithoutCrashing();
    void keyboardShortcuts();
    void newChatFocusesInput();
    void fullChatHistoryIsLoadedBeforeSending();

private:
    QTemporaryDir m_home;
};

void TestMainWindow::initTestCase() {
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", (m_home.path() + QStringLiteral("/.config")).toUtf8());
    qputenv("XDG_DATA_HOME", (m_home.path() + QStringLiteral("/.local/share")).toUtf8());
    QStandardPaths::setTestModeEnabled(true);
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

// Regression: a long chat is loaded into the session only page by page (the
// last 50 messages) for the UI. When sending, the full history must be
// loaded first, otherwise the model's context misses the older messages.
void TestMainWindow::fullChatHistoryIsLoadedBeforeSending() {
    constexpr int kStoredMessages = 60;
    {
        MainWindow window;
        window.show();
        auto &registry = window.registryForTest();
        auto &backend = registry.add(QStringLiteral("Ollama"), BackendKind::Ollama);
        registry.save();
        ChatSession *session = window.currentSession();
        QVERIFY(session);
        session->setBackendId(backend.id.toString());
        session->setModel(QStringLiteral("mock-model"));
        for (int i = 0; i < kStoredMessages; ++i)
            session->appendMessage(Role::User, QStringLiteral("q%1").arg(i));
    }

    // Reopen with the same (temp) config/data dir: only the last page is
    // loaded from the database.
    MainWindow window;
    window.show();
    ChatSession *session = window.currentSession();
    QVERIFY(session);
    QCOMPARE(session->messages().size(), 50);
    session->setModel(QStringLiteral("mock-model"));

    // The send itself fails (no server at the backend URL), which is fine:
    // the history load happens synchronously before the request.
    window.sendTextForTest(QStringLiteral("hello"));
    QCOMPARE(session->messages().size(), kStoredMessages + 2); // + user msg + system prompt
    QCOMPARE(session->messages().constFirst().role, Role::System);
    QVERIFY(!session->messages().constFirst().content.contains(QStringLiteral("{{")));
}

int main(int argc, char *argv[]) {
    // MainWindow is a QWidget, so the test needs a QApplication; force the
    // offscreen platform like tst_chatview does so CI needs no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mainwindow.moc"
