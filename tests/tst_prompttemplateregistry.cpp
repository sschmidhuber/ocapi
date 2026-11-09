#include "core/prompttemplateregistry.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

class TestPromptTemplateRegistry : public QObject {
    Q_OBJECT

private slots:
    void addUpdateRemoveFind();
    void changedSignalEmitted();
    void saveLoadRoundtripWithAttachments();
};

void TestPromptTemplateRegistry::addUpdateRemoveFind() {
    PromptTemplateRegistry registry;
    QVERIFY(registry.templates().isEmpty());
    QVERIFY(!registry.find(QUuid::createUuid()));

    PromptTemplate t;
    t.name = QStringLiteral("Summarize");
    t.text = QStringLiteral("Summarize the following text:");
    auto &stored = registry.add(t);
    QCOMPARE(registry.templates().size(), 1);
    QVERIFY(!stored.id.isNull());
    QCOMPARE(registry.find(stored.id)->name, QStringLiteral("Summarize"));

    PromptTemplate updated = *registry.find(stored.id);
    updated.text = QStringLiteral("Summarize briefly:");
    registry.update(updated);
    QCOMPARE(registry.templates().size(), 1);
    QCOMPARE(registry.find(stored.id)->text, QStringLiteral("Summarize briefly:"));

    registry.remove(stored.id);
    QVERIFY(registry.templates().isEmpty());
    QVERIFY(!registry.find(stored.id));
}

void TestPromptTemplateRegistry::changedSignalEmitted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PromptTemplateRegistry registry;
    {
        QSettings file(dir.filePath(QStringLiteral("okapi.conf")), QSettings::IniFormat);
        registry.load(file);
    }
    QSignalSpy spy(&registry, &PromptTemplateRegistry::changed);
    PromptTemplate t;
    t.name = QStringLiteral("t");
    t.text = QStringLiteral("x");
    auto &stored = registry.add(t);
    QCOMPARE(spy.count(), 1);
    registry.update(*registry.find(stored.id));
    QCOMPARE(spy.count(), 2);
    registry.remove(stored.id);
    QCOMPARE(spy.count(), 3);
}

void TestPromptTemplateRegistry::saveLoadRoundtripWithAttachments() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("okapi.conf"));

    PromptTemplateRegistry registry;
    {
        QSettings file(path, QSettings::IniFormat);
        registry.load(file);
    }
    PromptTemplate t;
    t.name = QStringLiteral("With files");
    t.text = QStringLiteral("Analyze:");
    ImageAttachment image;
    image.fileName = QStringLiteral("chart.png");
    image.mimeType = QStringLiteral("image/png");
    image.sourcePath = QStringLiteral("/tmp/chart.png");
    image.width = 800;
    image.height = 600;
    image.data = QByteArray("fakepng");
    t.images.append(image);
    FileAttachment file;
    file.fileName = QStringLiteral("notes.txt");
    file.sourcePath = QStringLiteral("/tmp/notes.txt");
    file.thumbnail = QByteArray("thumb");
    file.extractedText = QStringLiteral("some text");
    t.files.append(file);
    const QUuid id = registry.add(t).id;

    PromptTemplateRegistry loaded;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded.load(file);
    }
    QCOMPARE(loaded.templates().size(), 1);
    const PromptTemplate *restored = loaded.find(id);
    QVERIFY(restored);
    QCOMPARE(restored->name, QStringLiteral("With files"));
    QCOMPARE(restored->text, QStringLiteral("Analyze:"));
    QCOMPARE(restored->images.size(), 1);
    QCOMPARE(restored->images.first().fileName, QStringLiteral("chart.png"));
    QCOMPARE(restored->images.first().data, QByteArray("fakepng"));
    QCOMPARE(restored->images.first().width, 800);
    QCOMPARE(restored->files.size(), 1);
    QCOMPARE(restored->files.first().fileName, QStringLiteral("notes.txt"));
    QCOMPARE(restored->files.first().extractedText, QStringLiteral("some text"));
    QCOMPARE(restored->files.first().thumbnail, QByteArray("thumb"));
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestPromptTemplateRegistry test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_prompttemplateregistry.moc"
