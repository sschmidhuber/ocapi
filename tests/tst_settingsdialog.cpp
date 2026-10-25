#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/sttregistry.h"
#include "core/ttsregistry.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtTest>

#include <memory>

using namespace ocapi;
namespace {

QListWidget *categoriesList(const SettingsDialog &dialog) {
    return dialog.findChild<QListWidget *>();
}

QStackedWidget *stackOf(const SettingsDialog &dialog) {
    return dialog.findChild<QStackedWidget *>();
}

QTabWidget *tabsOf(const SettingsDialog &dialog, int categoryRow) {
    auto *stack = stackOf(dialog);
    return stack ? qobject_cast<QTabWidget *>(stack->widget(categoryRow)) : nullptr;
}

} // namespace

class TestSettingsDialog : public QObject {
    Q_OBJECT

private slots:
    void categoriesAreTopLevelItems();
    void pagesAreTabsOfTheirCategory();
    void selectPageSelectsCategoryAndTab();

private:
    SettingsDialog *makeDialog();
};

SettingsDialog *TestSettingsDialog::makeDialog() {
    BackendRegistry backends;
    McpRegistry mcpRegistry;
    McpManager mcpManager(&mcpRegistry);
    SttRegistry sttRegistry;
    TtsRegistry ttsRegistry;
    return new SettingsDialog(&backends, &mcpRegistry, &mcpManager, &sttRegistry, &ttsRegistry);
}

void TestSettingsDialog::categoriesAreTopLevelItems() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("Backend"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("Speech"));
}

void TestSettingsDialog::pagesAreTabsOfTheirCategory() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *backendTabs = tabsOf(*dialog, 0);
    auto *speechTabs = tabsOf(*dialog, 1);
    QVERIFY(backendTabs);
    QVERIFY(speechTabs);

    QCOMPARE(backendTabs->count(), 2);
    QCOMPARE(backendTabs->tabText(0), QStringLiteral("LLM"));
    QCOMPARE(backendTabs->tabText(1), QStringLiteral("MCP"));

    QCOMPARE(speechTabs->count(), 2);
    QCOMPARE(speechTabs->tabText(0), QStringLiteral("Speech-to-text"));
    QCOMPARE(speechTabs->tabText(1), QStringLiteral("Text-to-speech"));
}

void TestSettingsDialog::selectPageSelectsCategoryAndTab() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    auto *backendTabs = tabsOf(*dialog, 0);
    auto *speechTabs = tabsOf(*dialog, 1);

    dialog->selectPage(QStringLiteral("speech/tts"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(speechTabs->currentIndex(), 1);

    dialog->selectPage(QStringLiteral("backend/llm"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(backendTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("backend/mcp"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(backendTabs->currentIndex(), 1);
}

// The settings dialog is a QWidget, so the test needs a QApplication; force
// the offscreen platform because the CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestSettingsDialog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_settingsdialog.moc"
