#include "core/attachmentopener.h"

#include <QtTest>

using namespace ocapi;

class TestAttachmentOpener : public QObject {
    Q_OBJECT

private slots:
    void openRejectsEmptyPath();
    void openRejectsMissingFile();
    void openRejectsMissingFileWithError();
};

void TestAttachmentOpener::openRejectsEmptyPath() {
    QString error;
    QVERIFY(!AttachmentOpener::open(QString(), &error));
    QVERIFY(!error.isEmpty());
}

void TestAttachmentOpener::openRejectsMissingFile() {
    QVERIFY(!AttachmentOpener::open(QStringLiteral("/nonexistent/ocapi-test-file.bin")));
}

void TestAttachmentOpener::openRejectsMissingFileWithError() {
    QString error;
    QVERIFY(!AttachmentOpener::open(QStringLiteral("/nonexistent/ocapi-test-file.bin"), &error));
    QVERIFY(!error.isEmpty());
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestAttachmentOpener test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_attachmentopener.moc"
