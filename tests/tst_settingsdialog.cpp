#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/prompttemplateregistry.h"
#include "core/shortcutregistry.h"
#include "core/sttregistry.h"
#include "core/systemprompt.h"
#include "core/ttsregistry.h"
#include "core/uisettings.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QKeySequence>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest>

#include <memory>

using namespace okapi;
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
    void shortcutsPageListsAllActions();
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
    SystemPrompt systemPrompt;
    ShortcutRegistry shortcutRegistry;
    UiSettings uiSettings;
    PromptTemplateRegistry promptTemplates;
    return new SettingsDialog(&backends, &mcpRegistry, &mcpManager, &sttRegistry, &ttsRegistry,
                              &systemPrompt, &shortcutRegistry, &uiSettings, &promptTemplates);
}

void TestSettingsDialog::categoriesAreTopLevelItems() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    QVERIFY(list);
    QCOMPARE(list->count(), 4);
    QCOMPARE(list->item(0)->text(), QStringLiteral("General"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("Models"));
    QCOMPARE(list->item(2)->text(), QStringLiteral("Tools"));
    QCOMPARE(list->item(3)->text(), QStringLiteral("Shortcuts"));

    // Each category carries its themed icon with the bundled SVG fallback,
    // so it renders even without an icon theme providing the name.
    for (int i = 0; i < list->count(); ++i)
        QVERIFY(!list->item(i)->icon().isNull());
}

void TestSettingsDialog::pagesAreTabsOfTheirCategory() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *generalTabs = tabsOf(*dialog, 0);
    auto *modelsTabs = tabsOf(*dialog, 1);
    auto *toolsTabs = tabsOf(*dialog, 2);
    auto *shortcutsTabs = tabsOf(*dialog, 3);
    QVERIFY(generalTabs);
    QVERIFY(modelsTabs);
    QVERIFY(toolsTabs);
    QVERIFY(shortcutsTabs);

    QCOMPARE(generalTabs->count(), 3);
    QCOMPARE(generalTabs->tabText(0), QStringLiteral("System Prompt"));
    QCOMPARE(generalTabs->tabText(1), QStringLiteral("Prompt Templates"));
    QCOMPARE(generalTabs->tabText(2), QStringLiteral("User Interface"));

    QCOMPARE(modelsTabs->count(), 3);
    QCOMPARE(modelsTabs->tabText(0), QStringLiteral("LLM"));
    QCOMPARE(modelsTabs->tabText(1), QStringLiteral("STT"));
    QCOMPARE(modelsTabs->tabText(2), QStringLiteral("TTS"));

    QCOMPARE(toolsTabs->count(), 1);
    QCOMPARE(toolsTabs->tabText(0), QStringLiteral("MCP"));

    QCOMPARE(shortcutsTabs->count(), 1);
    QCOMPARE(shortcutsTabs->tabText(0), QStringLiteral("Shortcuts"));
}

void TestSettingsDialog::shortcutsPageListsAllActions() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    // Scope the lookup to the shortcuts tab: other pages (e.g. MCP) have
    // tables of their own.
    auto *shortcutsTabs = tabsOf(*dialog, 3);
    QVERIFY(shortcutsTabs);
    auto *table = shortcutsTabs->widget(0)->findChild<QTableWidget *>();
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Action"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Shortcut"));
    QCOMPARE(table->rowCount(), static_cast<int>(ShortcutRegistry::actions().size()));
    // The first row shows the first action with its default shortcut.
    const auto first = ShortcutRegistry::actions().constFirst();
    QCOMPARE(table->item(0, 0)->text(), first.description);
    QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toString(), first.id);
    QCOMPARE(table->item(0, 1)->text(), first.defaultSequence.toString(QKeySequence::NativeText));
}

void TestSettingsDialog::selectPageSelectsCategoryAndTab() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    auto *generalTabs = tabsOf(*dialog, 0);
    auto *modelsTabs = tabsOf(*dialog, 1);
    auto *toolsTabs = tabsOf(*dialog, 2);
    auto *shortcutsTabs = tabsOf(*dialog, 3);

    dialog->selectPage(QStringLiteral("tools/mcp"));
    QCOMPARE(list->currentRow(), 2);
    QCOMPARE(toolsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("shortcuts/shortcuts"));
    QCOMPARE(list->currentRow(), 3);
    QCOMPARE(shortcutsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("general/systemprompt"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("general/prompttemplates"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 1);

    dialog->selectPage(QStringLiteral("general/userinterface"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 2);

    dialog->selectPage(QStringLiteral("models/llm"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(modelsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("models/tts"));
    QCOMPARE(list->currentRow(), 1);
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
