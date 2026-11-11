#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/prompttemplateregistry.h"
#include "core/shortcutregistry.h"
#include "core/sttregistry.h"
#include "core/systemprompt.h"
#include "core/ttsregistry.h"
#include "core/uisettings.h"
#include "ui/mcppage.h"
#include "ui/settingsdialog.h"

#include <QApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QSplitter>
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
    void mcpHttpAuthModeShowsOnlyRelevantField();
    void sidebarKeepsMaxWidthWhenDialogGrows();

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
// The HTTP authorization mode radio buttons show only the relevant input
// field; switching the mode keeps both entered values.
void TestSettingsDialog::mcpHttpAuthModeShowsOnlyRelevantField() {
    McpRegistry registry;
    McpManager manager(&registry);
    // Add the server before constructing the page so its list is populated
    // and the first row is selected.
    registry.add(QStringLiteral("srv"), McpTransport::StreamableHttp);
    McpPage page(&registry, &manager);
    page.show();
    auto *list = page.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    list->setCurrentRow(0);

    auto *apiKeyRadio = page.findChild<QRadioButton *>(QStringLiteral("apiKeyRadio"));
    auto *headersRadio = page.findChild<QRadioButton *>(QStringLiteral("headersRadio"));
    auto *apiKeyEdit = page.findChild<QLineEdit *>(QStringLiteral("apiKeyEdit"));
    auto *headersEdit = page.findChild<QLineEdit *>(QStringLiteral("headersEdit"));
    QVERIFY(apiKeyRadio && headersRadio && apiKeyEdit && headersEdit);
    QCOMPARE(apiKeyRadio->text(), QStringLiteral("API token"));
    QCOMPARE(apiKeyEdit->placeholderText(), QStringLiteral("API token"));

    // Default: API token mode, headers field hidden.
    QVERIFY(apiKeyRadio->isChecked());
    QVERIFY(apiKeyEdit->isVisibleTo(&page));
    QVERIFY(!headersEdit->isVisibleTo(&page));

    QTest::keyClicks(apiKeyEdit, QStringLiteral("secret"));
    QCOMPARE(registry.servers().first().apiKey, QStringLiteral("secret"));

    // Switch to HTTP headers: only the headers field is visible, the API
    // token value is kept.
    headersRadio->setChecked(true);
    QVERIFY(!apiKeyEdit->isVisibleTo(&page));
    QVERIFY(headersEdit->isVisibleTo(&page));
    QCOMPARE(apiKeyEdit->text(), QStringLiteral("secret"));
    QCOMPARE(registry.servers().first().apiKey, QStringLiteral("secret"));

    QTest::keyClicks(headersEdit, QStringLiteral("X-A: 1"));
    QCOMPARE(registry.servers().first().headers, QStringList({QStringLiteral("X-A: 1")}));

    // Switch back: API token field visible again, headers value kept.
    apiKeyRadio->setChecked(true);
    QVERIFY(apiKeyEdit->isVisibleTo(&page));
    QVERIFY(!headersEdit->isVisibleTo(&page));
    QCOMPARE(headersEdit->text(), QStringLiteral("X-A: 1"));
    QCOMPARE(registry.servers().first().headers, QStringList({QStringLiteral("X-A: 1")}));
}

// Regression test: the settings sidebar (and the MCP server list) must not
// absorb extra width when the dialog grows — QSplitter's first-show sizing
// used to leave a large empty gap next to the list until the splitter handle
// was moved once. Stretch factor 0 keeps the list at its maximum width.
void TestSettingsDialog::sidebarKeepsMaxWidthWhenDialogGrows() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    dialog->resize(880, 580);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
    QTest::qWait(200);
    auto *splitter = dialog->findChild<QSplitter *>();
    auto *cats = dialog->findChild<QListWidget *>();
    if (!splitter || !cats)
        QFAIL("splitter or category list not found");
    dialog->resize(1400, 700);
    QTest::qWait(200);
    QVERIFY(splitter->sizes().first() <= cats->maximumWidth());

    auto *page = dialog->findChild<McpPage *>();
    if (!page)
        QFAIL("MCP page not found");
    dialog->selectPage(QStringLiteral("tools/mcp"));
    QTest::qWait(200);
    auto *inner = page->findChild<QSplitter *>();
    auto *srvList = page->findChild<QListWidget *>();
    if (!inner || !srvList)
        QFAIL("inner splitter or server list not found");
    QVERIFY(inner->sizes().first() <= srvList->maximumWidth() + 20);
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestSettingsDialog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_settingsdialog.moc"
