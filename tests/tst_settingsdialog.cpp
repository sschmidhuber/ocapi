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
    QCOMPARE(list->item(0)->text(), QStringLiteral("Models"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("Tools"));
}

void TestSettingsDialog::pagesAreTabsOfTheirCategory() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *modelsTabs = tabsOf(*dialog, 0);
    auto *toolsTabs = tabsOf(*dialog, 1);
    QVERIFY(modelsTabs);
    QVERIFY(toolsTabs);

    QCOMPARE(modelsTabs->count(), 3);
    QCOMPARE(modelsTabs->tabText(0), QStringLiteral("LLM"));
    QCOMPARE(modelsTabs->tabText(1), QStringLiteral("STT"));
    QCOMPARE(modelsTabs->tabText(2), QStringLiteral("TTS"));

    QCOMPARE(toolsTabs->count(), 1);
    QCOMPARE(toolsTabs->tabText(0), QStringLiteral("MCP"));
}

void TestSettingsDialog::selectPageSelectsCategoryAndTab() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    auto *modelsTabs = tabsOf(*dialog, 0);
    auto *toolsTabs = tabsOf(*dialog, 1);

    dialog->selectPage(QStringLiteral("tools/mcp"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(toolsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("models/llm"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(modelsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("models/tts"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(modelsTabs->currentIndex(), 2);
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
