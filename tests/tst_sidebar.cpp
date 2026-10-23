#include "core/chatsession.h"
#include "core/sessiongrouper.h"
#include "ui/sidebar.h"

#include <QApplication>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace ocapi;

namespace {

std::unique_ptr<ChatSession> makeSession(const QString &title, const QDateTime &activity) {
    auto session = std::make_unique<ChatSession>(title, QStringLiteral("backend"));
    session->setLastActivityAt(activity);
    return session;
}

QTreeWidgetItem *groupItem(QTreeWidget *tree, const QDateTime &activity) {
    const QString label = SessionGrouper::groupLabel(
        SessionGrouper::groupKey(activity, QDateTime::currentDateTime()));
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->text(0) == label)
            return item;
    }
    return nullptr;
}

} // namespace

class TestSidebar : public QObject {
    Q_OBJECT

private slots:
    void firstBuildExpandsOnlyToday();
    void rebuildPreservesExpandState();
    void collapsingAllGroupsStaysCollapsed();
    void currentSessionIdIsNullForGroupItems();
    void emptySessionsClearTree();
};

void TestSidebar::firstBuildExpandsOnlyToday() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    auto yesterday = makeSession(QStringLiteral("yesterday chat"), now.addDays(-1));
    auto lastWeek = makeSession(QStringLiteral("old chat"), now.addDays(-8));
    const QVector<ChatSession *> sessions{today.get(), yesterday.get(), lastWeek.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    QCOMPARE(tree->topLevelItemCount(), 3);
    auto *todayGroup = groupItem(tree, now);
    auto *yesterdayGroup = groupItem(tree, now.addDays(-1));
    auto *oldGroup = groupItem(tree, now.addDays(-8));
    QVERIFY(todayGroup);
    QVERIFY(yesterdayGroup);
    QVERIFY(oldGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(!yesterdayGroup->isExpanded());
    QVERIFY(!oldGroup->isExpanded());
    QCOMPARE(todayGroup->childCount(), 1);
    QCOMPARE(yesterdayGroup->childCount(), 1);
    QCOMPARE(oldGroup->childCount(), 1);
    QCOMPARE(todayGroup->child(0)->text(0), QStringLiteral("today chat"));
}

void TestSidebar::rebuildPreservesExpandState() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    auto yesterday = makeSession(QStringLiteral("yesterday chat"), now.addDays(-1));
    const QVector<ChatSession *> sessions{today.get(), yesterday.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *todayGroup = groupItem(tree, now);
    auto *yesterdayGroup = groupItem(tree, now.addDays(-1));
    QVERIFY(todayGroup && yesterdayGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(!yesterdayGroup->isExpanded());

    // The user expands Yesterday; a rebuild must keep that state.
    yesterdayGroup->setExpanded(true);
    sidebar.setSessions(sessions);

    todayGroup = groupItem(tree, now);
    yesterdayGroup = groupItem(tree, now.addDays(-1));
    QVERIFY(todayGroup && yesterdayGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(yesterdayGroup->isExpanded());
}

void TestSidebar::collapsingAllGroupsStaysCollapsed() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    const QVector<ChatSession *> sessions{today.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    QVERIFY(todayGroup->isExpanded());

    // Collapsing the only group must not re-expand it on the next rebuild.
    todayGroup->setExpanded(false);
    sidebar.setSessions(sessions);

    todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    QVERIFY(!todayGroup->isExpanded());
}

void TestSidebar::currentSessionIdIsNullForGroupItems() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    const QVector<ChatSession *> sessions{today.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *group = tree->topLevelItem(0);
    QVERIFY(group);
    tree->setCurrentItem(group);
    QVERIFY(sidebar.currentSessionId().isNull());

    tree->setCurrentItem(group->child(0));
    QCOMPARE(sidebar.currentSessionId(), today->id());
}

void TestSidebar::emptySessionsClearTree() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions({today.get()});
    QCOMPARE(tree->topLevelItemCount(), 1);

    sidebar.setSessions({});
    QCOMPARE(tree->topLevelItemCount(), 0);
}

// Sidebar is a QWidget, so the test needs a QApplication; force the
// offscreen platform because the CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestSidebar test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_sidebar.moc"
