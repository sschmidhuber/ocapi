#include "core/sessiongrouper.h"

#include <QLocale>
#include <QtTest>

using namespace ocapi;

class TestSessionGrouper : public QObject {
    Q_OBJECT

private slots:
    void todayGroupsAsToday();
    void yesterdayGroupsAsYesterday();
    void lastFiveDaysGroupByWeekday();
    void sameMonthOlderDaysGroupByDate();
    void sameYearOlderMonthsGroupByMonthName();
    void olderYearsGroupByYear();
    void invalidTimestampGivesEmptyKey();
    void labelsAreLocalized();
    void weekdayGroupsDoNotMergeAcrossWeeks();
    void groupKeyIsStableForSameDay();
};

namespace {

// Reference "now": Friday 2026-10-23 12:00 local time.
QDateTime reference() {
    return {QDate(2026, 10, 23), QTime(12, 0)};
}

} // namespace

void TestSessionGrouper::todayGroupsAsToday() {
    const QDateTime ts(QDate(2026, 10, 23), QTime(0, 1));
    QCOMPARE(SessionGrouper::groupKey(ts, reference()), QStringLiteral("today"));
    QCOMPARE(SessionGrouper::groupLabel(SessionGrouper::groupKey(ts, reference())),
             QStringLiteral("Today"));
}

void TestSessionGrouper::yesterdayGroupsAsYesterday() {
    const QDateTime ts(QDate(2026, 10, 22), QTime(23, 59));
    QCOMPARE(SessionGrouper::groupKey(ts, reference()), QStringLiteral("yesterday"));
    QCOMPARE(SessionGrouper::groupLabel(SessionGrouper::groupKey(ts, reference())),
             QStringLiteral("Yesterday"));
}

void TestSessionGrouper::lastFiveDaysGroupByWeekday() {
    const QLocale locale;
    // 2026-10-23 is a Friday. 2 days ago = Wednesday, 5 days ago = Sunday.
    for (int daysAgo = 2; daysAgo <= 5; ++daysAgo) {
        const QDate date = reference().date().addDays(-daysAgo);
        const QDateTime ts(date, QTime(10, 0));
        const QString key = SessionGrouper::groupKey(ts, reference());
        QVERIFY(key.startsWith(QStringLiteral("day:")));
        QCOMPARE(SessionGrouper::groupLabel(key),
                 locale.dayName(date.dayOfWeek(), QLocale::LongFormat));
    }
}

void TestSessionGrouper::sameMonthOlderDaysGroupByDate() {
    const QLocale locale;
    // 2026-10-06 is in the same month but more than 5 days ago.
    const QDateTime ts(QDate(2026, 10, 6), QTime(10, 0));
    const QString key = SessionGrouper::groupKey(ts, reference());
    QVERIFY(key.startsWith(QStringLiteral("monthday:")));
    QCOMPARE(SessionGrouper::groupLabel(key),
             QStringLiteral("6 %1").arg(locale.monthName(10, QLocale::ShortFormat)));
}

void TestSessionGrouper::sameYearOlderMonthsGroupByMonthName() {
    const QLocale locale;
    // 2026-08-15 is in the same year but a different month.
    const QDateTime ts(QDate(2026, 8, 15), QTime(10, 0));
    const QString key = SessionGrouper::groupKey(ts, reference());
    QVERIFY(key.startsWith(QStringLiteral("month:")));
    QCOMPARE(SessionGrouper::groupLabel(key), locale.monthName(8, QLocale::LongFormat));
}

void TestSessionGrouper::olderYearsGroupByYear() {
    const QDateTime ts(QDate(2024, 3, 10), QTime(10, 0));
    const QString key = SessionGrouper::groupKey(ts, reference());
    QCOMPARE(key, QStringLiteral("year:2024"));
    QCOMPARE(SessionGrouper::groupLabel(key), QStringLiteral("2024"));
}

void TestSessionGrouper::invalidTimestampGivesEmptyKey() {
    QVERIFY(SessionGrouper::groupKey(QDateTime(), reference()).isEmpty());
}

void TestSessionGrouper::labelsAreLocalized() {
    QCOMPARE(SessionGrouper::groupLabel(QStringLiteral("today")), QStringLiteral("Today"));
    QCOMPARE(SessionGrouper::groupLabel(QStringLiteral("yesterday")), QStringLiteral("Yesterday"));
    QCOMPARE(SessionGrouper::groupLabel(QStringLiteral("year:2023")), QStringLiteral("2023"));
    // Unknown keys pass through unchanged.
    QCOMPARE(SessionGrouper::groupLabel(QStringLiteral("something")), QStringLiteral("something"));
}

void TestSessionGrouper::weekdayGroupsDoNotMergeAcrossWeeks() {
    // A Wednesday 2 days ago groups by weekday; the Wednesday 9 days ago
    // is beyond the 5-day weekday window and groups by date instead.
    const QDateTime thisWeek(QDate(2026, 10, 21), QTime(10, 0)); // Wednesday, 2 days ago
    const QDateTime lastWeek(QDate(2026, 10, 14), QTime(10, 0)); // Wednesday, 9 days ago
    const QString keyThisWeek = SessionGrouper::groupKey(thisWeek, reference());
    const QString keyLastWeek = SessionGrouper::groupKey(lastWeek, reference());
    // 9 days ago is beyond the 5-day weekday window, so it groups by date.
    QVERIFY(keyThisWeek.startsWith(QStringLiteral("day:")));
    QVERIFY(keyLastWeek.startsWith(QStringLiteral("monthday:")));
    QVERIFY(keyThisWeek != keyLastWeek);
}

void TestSessionGrouper::groupKeyIsStableForSameDay() {
    const QDateTime morning(QDate(2026, 10, 23), QTime(8, 0));
    const QDateTime evening(QDate(2026, 10, 23), QTime(20, 0));
    QCOMPARE(SessionGrouper::groupKey(morning, reference()),
             SessionGrouper::groupKey(evening, reference()));
}

QTEST_GUILESS_MAIN(TestSessionGrouper)
#include "tst_sessiongrouper.moc"
