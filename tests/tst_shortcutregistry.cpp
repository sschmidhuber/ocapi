#include "core/shortcutregistry.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

class TestShortcutRegistry : public QObject {
    Q_OBJECT

private slots:
    void catalogContainsAllActions();
    void defaultsAreReturnedWithoutOverrides();
    void setSequenceOverridesAndEmitsChanged();
    void emptyOverrideDisablesShortcut();
    void resetRestoresDefault();
    void conflictIsDetected();
    void saveLoadRoundtrip();
    void unknownIdsAreIgnored();
};

void TestShortcutRegistry::catalogContainsAllActions() {
    const auto actions = ShortcutRegistry::actions();
    QCOMPARE(static_cast<int>(actions.size()), 6);
    QStringList ids;
    for (const auto &action : actions) {
        ids.append(action.id);
        QVERIFY(!action.description.isEmpty());
        QVERIFY(!action.defaultSequence.isEmpty());
    }
    QVERIFY(ids.contains(QStringLiteral("newChat")));
    QVERIFY(ids.contains(QStringLiteral("toggleSidebar")));
    QVERIFY(ids.contains(QStringLiteral("nextChat")));
    QVERIFY(ids.contains(QStringLiteral("previousChat")));
    QVERIFY(ids.contains(QStringLiteral("dictate")));
    QVERIFY(ids.contains(QStringLiteral("promptTemplates")));
    // The prompt-template trigger is composer-local, not a window shortcut.
    ShortcutAction trigger;
    bool found = false;
    for (const auto &a : actions) {
        if (a.id == QStringLiteral("promptTemplates")) {
            trigger = a;
            found = true;
        }
    }
    QVERIFY(found);
    QCOMPARE(trigger.defaultSequence, QKeySequence(QStringLiteral("/")));
    QVERIFY(!trigger.windowScope);
}

void TestShortcutRegistry::defaultsAreReturnedWithoutOverrides() {
    ShortcutRegistry registry;
    QCOMPARE(registry.sequence(QStringLiteral("newChat")), QKeySequence(QStringLiteral("Ctrl+N")));
    QCOMPARE(registry.sequence(QStringLiteral("toggleSidebar")), QKeySequence(Qt::Key_F9));
    QCOMPARE(registry.sequence(QStringLiteral("nextChat")),
             QKeySequence(QStringLiteral("Ctrl+Tab")));
    QCOMPARE(registry.sequence(QStringLiteral("previousChat")),
             QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    QCOMPARE(registry.sequence(QStringLiteral("dictate")), QKeySequence(QStringLiteral("Ctrl+.")));
    QVERIFY(registry.sequence(QStringLiteral("no-such-action")).isEmpty());
}

void TestShortcutRegistry::setSequenceOverridesAndEmitsChanged() {
    ShortcutRegistry registry;
    QSignalSpy spy(&registry, &ShortcutRegistry::changed);
    registry.setSequence(QStringLiteral("newChat"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(registry.sequence(QStringLiteral("newChat")),
             QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    // Other actions keep their defaults.
    QCOMPARE(registry.sequence(QStringLiteral("dictate")), QKeySequence(QStringLiteral("Ctrl+.")));
}

void TestShortcutRegistry::emptyOverrideDisablesShortcut() {
    ShortcutRegistry registry;
    registry.setSequence(QStringLiteral("newChat"), QKeySequence());
    QVERIFY(registry.sequence(QStringLiteral("newChat")).isEmpty());
    QVERIFY(!ShortcutRegistry::defaultSequence(QStringLiteral("newChat")).isEmpty());
}

void TestShortcutRegistry::resetRestoresDefault() {
    ShortcutRegistry registry;
    QSignalSpy spy(&registry, &ShortcutRegistry::changed);
    registry.setSequence(QStringLiteral("newChat"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    QCOMPARE(spy.count(), 1);
    registry.resetToDefault(QStringLiteral("newChat"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(registry.sequence(QStringLiteral("newChat")), QKeySequence(QStringLiteral("Ctrl+N")));
    // Resetting an action without an override does not emit changed().
    registry.resetToDefault(QStringLiteral("newChat"));
    QCOMPARE(spy.count(), 2);
}

void TestShortcutRegistry::conflictIsDetected() {
    ShortcutRegistry registry;
    // F9 is the default of "toggleSidebar"; assigning it to "newChat" conflicts.
    QCOMPARE(registry.conflictingAction(QStringLiteral("newChat"), QKeySequence(Qt::Key_F9)),
             ShortcutRegistry::actions().at(1).description);
    // An action never conflicts with itself, and empty sequences never conflict.
    QVERIFY(registry.conflictingAction(QStringLiteral("toggleSidebar"), QKeySequence(Qt::Key_F9))
                .isEmpty());
    QVERIFY(registry.conflictingAction(QStringLiteral("newChat"), QKeySequence()).isEmpty());
    QVERIFY(registry
                .conflictingAction(QStringLiteral("newChat"),
                                   QKeySequence(QStringLiteral("Ctrl+Shift+N")))
                .isEmpty());
}

void TestShortcutRegistry::saveLoadRoundtrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("okapi.conf"));

    ShortcutRegistry registry;
    registry.setSequence(QStringLiteral("newChat"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    registry.setSequence(QStringLiteral("dictate"), QKeySequence()); // disabled
    {
        QSettings settings(path, QSettings::IniFormat);
        registry.save(settings);
    }

    ShortcutRegistry loaded;
    {
        QSettings settings(path, QSettings::IniFormat);
        loaded.load(settings);
    }
    QCOMPARE(loaded.sequence(QStringLiteral("newChat")),
             QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    QVERIFY(loaded.sequence(QStringLiteral("dictate")).isEmpty());
    QCOMPARE(loaded.sequence(QStringLiteral("toggleSidebar")), QKeySequence(Qt::Key_F9));
}

void TestShortcutRegistry::unknownIdsAreIgnored() {
    ShortcutRegistry registry;
    QSignalSpy spy(&registry, &ShortcutRegistry::changed);
    registry.setSequence(QStringLiteral("no-such-action"), QKeySequence(QStringLiteral("Ctrl+Q")));
    QCOMPARE(spy.count(), 0);
    QVERIFY(registry.sequence(QStringLiteral("no-such-action")).isEmpty());
    QVERIFY(ShortcutRegistry::defaultSequence(QStringLiteral("no-such-action")).isEmpty());
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestShortcutRegistry test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_shortcutregistry.moc"
