#include "core/systemprompt.h"

#include <QLocale>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestSystemPrompt : public QObject {
    Q_OBJECT

private slots:
    void defaultTemplateContainsAllFields();
    void substituteReplacesAllFields();
    void substituteLeavesUnknownFieldsIntact();
    void substituteIsCaseInsensitive();
    void loadSaveRoundtrip();
    void loadFallsBackToDefault();
};

void TestSystemPrompt::defaultTemplateContainsAllFields() {
    const QString templ = SystemPrompt::defaultTemplate();
    QVERIFY(templ.contains(QStringLiteral("{{date}}")));
    QVERIFY(templ.contains(QStringLiteral("{{time}}")));
    QVERIFY(templ.contains(QStringLiteral("{{language}}")));
    QVERIFY(templ.contains(QStringLiteral("{{os}}")));
}

void TestSystemPrompt::substituteReplacesAllFields() {
    const QDateTime now(QDate(2026, 11, 5), QTime(14, 30, 0));
    const QString templ = QStringLiteral("date={{date}} time={{time}} lang={{language}} os={{os}}");
    const QString out = SystemPrompt::substitute(templ, now);
    const QLocale locale = QLocale::system();
    QVERIFY(!out.contains(QStringLiteral("{{")));
    QVERIFY(out.contains(locale.toString(now.date(), QLocale::ShortFormat)));
    QVERIFY(out.contains(locale.toString(now.time(), QLocale::ShortFormat)));
    QVERIFY(out.contains(locale.name()));
    QVERIFY(out.contains(QSysInfo::prettyProductName()));
}

void TestSystemPrompt::substituteLeavesUnknownFieldsIntact() {
    const QString out = SystemPrompt::substitute(QStringLiteral("keep {{unknown}} and {{date}}"),
                                                 QDateTime(QDate(2026, 1, 1), QTime(0, 0)));
    QVERIFY(out.contains(QStringLiteral("{{unknown}}")));
    QVERIFY(!out.contains(QStringLiteral("{{date}}")));
}

void TestSystemPrompt::substituteIsCaseInsensitive() {
    const QString out = SystemPrompt::substitute(QStringLiteral("{{DATE}} {{Os}}"),
                                                 QDateTime(QDate(2026, 1, 1), QTime(0, 0)));
    QVERIFY(!out.contains(QStringLiteral("{{")));
}

void TestSystemPrompt::loadSaveRoundtrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/ocapi.conf");
    {
        QSettings settings(path, QSettings::IniFormat);
        SystemPrompt prompt;
        prompt.setText(QStringLiteral("Custom prompt with {{date}}"));
        prompt.save(settings);
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        SystemPrompt prompt;
        prompt.load(settings);
        QCOMPARE(prompt.text(), QStringLiteral("Custom prompt with {{date}}"));
    }
}

void TestSystemPrompt::loadFallsBackToDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.path() + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    SystemPrompt prompt;
    prompt.load(settings);
    QCOMPARE(prompt.text(), SystemPrompt::defaultTemplate());
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    TestSystemPrompt test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_systemprompt.moc"
