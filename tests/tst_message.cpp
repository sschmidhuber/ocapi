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
    void imagesDefaultEmpty();
    void imageAttachmentsAreKept();
    void filesDefaultEmpty();
    void fileAttachmentsAreKept();
    void isEmptyCoversTextImagesAndFiles();
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

void TestMessage::imagesDefaultEmpty() {
    const Message m;
    QVERIFY(m.images.isEmpty());
    QVERIFY(!m.hasImages());
}

void TestMessage::imageAttachmentsAreKept() {
    Message m;
    ImageAttachment image;
    image.mimeType = QStringLiteral("image/png");
    image.data = QByteArrayLiteral("\x89PNG");
    m.images.push_back(image);
    QVERIFY(m.hasImages());
    QCOMPARE(m.images.size(), 1);
    QCOMPARE(m.images.first().mimeType, QStringLiteral("image/png"));
}

void TestMessage::filesDefaultEmpty() {
    const Message m;
    QVERIFY(m.files.isEmpty());
    QVERIFY(!m.hasFiles());
}

void TestMessage::fileAttachmentsAreKept() {
    Message m;
    FileAttachment file;
    file.fileName = QStringLiteral("report.pdf");
    file.thumbnail = QByteArrayLiteral("\x89PNG");
    m.files.push_back(file);
    QVERIFY(m.hasFiles());
    QCOMPARE(m.files.size(), 1);
    QCOMPARE(m.files.first().fileName, QStringLiteral("report.pdf"));
    // An invalid thumbnail blob decodes to a null image, which is handled.
    QVERIFY(m.files.first().decodeThumbnail().isNull());
}

void TestMessage::isEmptyCoversTextImagesAndFiles() {
    Message m;
    QVERIFY(m.isEmpty());
    ImageAttachment image;
    image.data = QByteArrayLiteral("x");
    m.images.push_back(image);
    QVERIFY(!m.isEmpty());
    m.images.clear();
    FileAttachment file;
    file.fileName = QStringLiteral("a.txt");
    m.files.push_back(file);
    QVERIFY(!m.isEmpty());
    m.files.clear();
    m.content = QStringLiteral("hello");
    QVERIFY(!m.isEmpty());
}

QTEST_GUILESS_MAIN(TestMessage)
#include "tst_message.moc"
