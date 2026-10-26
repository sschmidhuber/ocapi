#include "core/sttregistry.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestSttRegistry : public QObject {
    Q_OBJECT

private slots:
    void init();
    void addAppendsServiceWithDefaults();
    void removeDeletesServiceAndFixesActiveId();
    void enabledFiltersDisabled();
    void settingsRoundTrip();
    void inputDevicePersists();
    void activeServiceIdPersists();
    void saveAfterSettingsObjectDestroyed();

private:
    [[nodiscard]] QSettings settings() const;

    QTemporaryDir m_tempDir;
    QString m_filePath;
};

void TestSttRegistry::init() {
    static int counter = 0;
    QVERIFY(m_tempDir.isValid());
    m_filePath = m_tempDir.filePath(QStringLiteral("stt-%1.conf").arg(counter++));
}

QSettings TestSttRegistry::settings() const {
    return {m_filePath, QSettings::IniFormat};
}

void TestSttRegistry::addAppendsServiceWithDefaults() {
    SttRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    QSignalSpy spy(&registry, &SttRegistry::changed);
    auto &config = registry.add(QStringLiteral("Local"), SttServiceKind::WhisperApi);
    QCOMPARE(registry.services().size(), 1);
    QCOMPARE(config.name, QStringLiteral("Local"));
    QCOMPARE(config.kind, SttServiceKind::WhisperApi);
    QCOMPARE(config.baseUrl, SttServiceConfig::defaultBaseUrl(SttServiceKind::WhisperApi));
    QCOMPARE(spy.count(), 1);
}

void TestSttRegistry::removeDeletesServiceAndFixesActiveId() {
    SttRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    // The id is captured by value: add() returns a reference into the
    // services vector, which a later add() reallocates.
    const QUuid firstId = registry.add(QStringLiteral("a"), SttServiceKind::OpenAiApi).id;
    registry.add(QStringLiteral("b"), SttServiceKind::Groq);
    QCOMPARE(registry.activeServiceId(), firstId.toString(QUuid::WithoutBraces));
    QVERIFY(registry.remove(firstId));
    QCOMPARE(registry.activeServiceId(),
             registry.services().first().id.toString(QUuid::WithoutBraces));
}

void TestSttRegistry::enabledFiltersDisabled() {
    SttRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    auto &enabled = registry.add(QStringLiteral("on"), SttServiceKind::WhisperCpp);
    auto &disabled = registry.add(QStringLiteral("off"), SttServiceKind::Groq);
    disabled.enabled = false;
    const auto enabledServices = registry.enabled();
    QCOMPARE(enabledServices.size(), 1);
    QCOMPARE(enabledServices.first().id, enabled.id);
}

void TestSttRegistry::settingsRoundTrip() {
    {
        SttRegistry registry;
        QSettings s = settings();
        registry.loadSettings(s);
        auto &config = registry.add(QStringLiteral("Remote"), SttServiceKind::Mistral);
        config.apiKey = QStringLiteral("key");
        registry.save();
    }
    SttRegistry restored;
    QSettings s = settings();
    restored.loadSettings(s);
    QCOMPARE(restored.services().size(), 1);
    QCOMPARE(restored.services().first().name, QStringLiteral("Remote"));
    QCOMPARE(restored.services().first().kind, SttServiceKind::Mistral);
    QCOMPARE(restored.services().first().apiKey, QStringLiteral("key"));
}

void TestSttRegistry::inputDevicePersists() {
    {
        SttRegistry registry;
        QSettings s = settings();
        registry.loadSettings(s);
        registry.setInputDevice(QStringLiteral("alsa:hw0"));
        QCOMPARE(registry.inputDevice(), QStringLiteral("alsa:hw0"));
    }
    SttRegistry restored;
    QSettings s = settings();
    restored.loadSettings(s);
    QCOMPARE(restored.inputDevice(), QStringLiteral("alsa:hw0"));
}

void TestSttRegistry::activeServiceIdPersists() {
    SttRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    const auto id = registry.add(QStringLiteral("x"), SttServiceKind::WhisperApi).id;
    registry.setActiveServiceId(id.toString(QUuid::WithoutBraces));
    QCOMPARE(registry.activeServiceId(), id.toString(QUuid::WithoutBraces));

    SttRegistry restored;
    QSettings s2 = settings();
    restored.loadSettings(s2);
    QCOMPARE(restored.activeServiceId(), id.toString(QUuid::WithoutBraces));
}

void TestSttRegistry::saveAfterSettingsObjectDestroyed() {
    // Regression (#60): loadSettings() used to keep a pointer to the
    // caller's QSettings. MainWindow passes a stack-local object, so the
    // first save() after it went out of scope (e.g. clicking "Add" in the
    // STT settings) dereferenced a dangling pointer and crashed.
    {
        QSettings s = settings();
        s.setValue(QStringLiteral("stt/services"), QStringLiteral("[]"));
    }
    SttRegistry registry;
    {
        QSettings s = settings();
        registry.loadSettings(s);
    }
    registry.add(QStringLiteral("Added after load"), SttServiceKind::WhisperApi);

    SttRegistry restored;
    QSettings s2 = settings();
    restored.loadSettings(s2);
    QCOMPARE(restored.services().size(), 1);
    QCOMPARE(restored.services().first().name, QStringLiteral("Added after load"));
}

QTEST_GUILESS_MAIN(TestSttRegistry)
#include "tst_sttregistry.moc"
