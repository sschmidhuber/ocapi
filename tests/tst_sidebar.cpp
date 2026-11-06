#include "core/chatsession.h"
#include "core/sessiongrouper.h"
#include "ui/sidebar.h"

#include <QApplication>
#include <QPushButton>
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
    void sidebarHasNoToolbarButtons();
    void indicatorsBadgeRunningAndUnreadChats();
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

void TestSidebar::sidebarHasNoToolbarButtons() {
    // New chat / settings live in the main window toolbar now; the sidebar
    // only contains the history tree.
    Sidebar sidebar;
    QVERIFY(sidebar.findChildren<QPushButton *>().isEmpty());
    QVERIFY(sidebar.findChild<QTreeWidget *>() != nullptr);
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

// The running/unread badges: a running chat shows a spinner icon, an
// unread chat a dot; a chat that is neither has no icon. Rebuilding the
// tree (setSessions) must keep the badges.
void TestSidebar::indicatorsBadgeRunningAndUnreadChats() {
    const QDateTime now = QDateTime::currentDateTime();
    auto running = makeSession(QStringLiteral("running chat"), now);
    auto unread = makeSession(QStringLiteral("unread chat"), now);
    auto plain = makeSession(QStringLiteral("plain chat"), now);
    const QVector<ChatSession *> sessions{running.get(), unread.get(), plain.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    sidebar.setIndicators({running->id()}, {unread->id()});

    auto *group = groupItem(tree, now);
    QVERIFY(group);
    QCOMPARE(group->childCount(), 3);
    bool sawRunning = false;
    bool sawUnread = false;
    bool sawPlain = false;
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const QUuid id(item->data(0, Qt::UserRole).toString());
        if (id == running->id()) {
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("generating")));
            sawRunning = true;
        } else if (id == unread->id()) {
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("unread")));
            sawUnread = true;
        } else {
            QVERIFY(item->icon(0).isNull());
            QCOMPARE(item->toolTip(0), item->text(0));
            sawPlain = true;
        }
    }
    QVERIFY(sawRunning && sawUnread && sawPlain);

    // A rebuild must keep the badges.
    sidebar.setSessions(sessions);
    group = groupItem(tree, now);
    QVERIFY(group);
    bool runningKept = false;
    bool unreadKept = false;
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const QUuid id(item->data(0, Qt::UserRole).toString());
        if (id == running->id() && !item->icon(0).isNull())
            runningKept = true;
        if (id == unread->id() && !item->icon(0).isNull())
            unreadKept = true;
    }
    QVERIFY(runningKept);
    QVERIFY(unreadKept);

    // Clearing the indicators removes the badges again.
    sidebar.setIndicators({}, {});
    group = groupItem(tree, now);
    QVERIFY(group);
    for (int i = 0; i < group->childCount(); ++i)
        QVERIFY(group->child(i)->icon(0).isNull());
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
