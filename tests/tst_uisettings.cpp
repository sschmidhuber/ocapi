#include "core/uisettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestUiSettings : public QObject {
    Q_OBJECT

private slots:
    void defaultsToHiddenThroughput();
    void setShowThroughputEmitsChanged();
    void saveLoadRoundtrip();
    void setSameValueDoesNotEmit();
};

void TestUiSettings::defaultsToHiddenThroughput() {
    UiSettings settings;
    QVERIFY(!settings.showThroughput());
}

void TestUiSettings::setShowThroughputEmitsChanged() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UiSettings settings;
    {
        QSettings file(dir.filePath(QStringLiteral("ocapi.conf")), QSettings::IniFormat);
        settings.load(file);
    }
    QSignalSpy spy(&settings, &UiSettings::changed);
    settings.setShowThroughput(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(settings.showThroughput());
    settings.setShowThroughput(false);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!settings.showThroughput());
}

void TestUiSettings::saveLoadRoundtrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ocapi.conf"));

    UiSettings settings;
    {
        QSettings file(path, QSettings::IniFormat);
        settings.load(file);
    }
    settings.setShowThroughput(true); // saves immediately via the persist path

    UiSettings loaded;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded.load(file);
    }
    QVERIFY(loaded.showThroughput());

    loaded.setShowThroughput(false);
    UiSettings reloaded;
    {
        QSettings file(path, QSettings::IniFormat);
        reloaded.load(file);
    }
    QVERIFY(!reloaded.showThroughput());
}

void TestUiSettings::setSameValueDoesNotEmit() {
    UiSettings settings;
    QSignalSpy spy(&settings, &UiSettings::changed);
    settings.setShowThroughput(false); // already the default
    QCOMPARE(spy.count(), 0);
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestUiSettings test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_uisettings.moc"
