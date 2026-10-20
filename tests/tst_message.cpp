#include "core/message.h"

#include <QtTest>

using namespace ocapi;

class TestMessage : public QObject {
    Q_OBJECT

private slots:
    void roleNameMapsEveryRole();
    void defaultMessageIsUserRoleNotStreamingNoError();
    void eachMessageHasUniqueId();
    void timestampIsSet();
};

void TestMessage::roleNameMapsEveryRole() {
    Message m;
    m.role = Role::System;
    QCOMPARE(m.roleName(), QStringLiteral("system"));
    m.role = Role::User;
    QCOMPARE(m.roleName(), QStringLiteral("user"));
    m.role = Role::Assistant;
    QCOMPARE(m.roleName(), QStringLiteral("assistant"));
    m.role = Role::Tool;
    QCOMPARE(m.roleName(), QStringLiteral("tool"));
}

void TestMessage::defaultMessageIsUserRoleNotStreamingNoError() {
    const Message m;
    QCOMPARE(m.role, Role::User);
    QVERIFY(!m.streaming);
    QVERIFY(m.error.isEmpty());
    QVERIFY(m.content.isEmpty());
}

void TestMessage::eachMessageHasUniqueId() {
    const Message a;
    const Message b;
    QVERIFY(!a.id.isNull());
    QVERIFY(a.id != b.id);
}

void TestMessage::timestampIsSet() {
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    const Message m;
    const QDateTime after = QDateTime::currentDateTimeUtc().addSecs(1);
    QVERIFY(m.timestamp.isValid());
    QVERIFY(m.timestamp.toUTC() >= before);
    QVERIFY(m.timestamp.toUTC() <= after);
}

QTEST_GUILESS_MAIN(TestMessage)
#include "tst_message.moc"
