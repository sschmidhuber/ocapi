#include "core/ttsregistry.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestTtsRegistry : public QObject {
    Q_OBJECT

private slots:
    void init();
    void addAppendsServiceWithDefaults();
    void removeDeletesServiceAndFixesActiveId();
    void enabledFiltersDisabled();
    void settingsRoundTrip();
    void outputDevicePersists();
    void activeServiceIdPersists();
    void speakResponsesPersists();
    void saveAfterSettingsObjectDestroyed();

private:
    [[nodiscard]] QSettings settings() const;

    QTemporaryDir m_tempDir;
    QString m_filePath;
};

void TestTtsRegistry::init() {
    static int counter = 0;
    QVERIFY(m_tempDir.isValid());
    m_filePath = m_tempDir.filePath(QStringLiteral("tts-%1.conf").arg(counter++));
}

QSettings TestTtsRegistry::settings() const {
    return {m_filePath, QSettings::IniFormat};
}

void TestTtsRegistry::addAppendsServiceWithDefaults() {
    TtsRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    QSignalSpy spy(&registry, &TtsRegistry::changed);
    auto &config = registry.add(QStringLiteral("Local"), TtsServiceKind::Piper);
    QCOMPARE(registry.services().size(), 1);
    QCOMPARE(config.name, QStringLiteral("Local"));
    QCOMPARE(config.kind, TtsServiceKind::Piper);
    QCOMPARE(config.baseUrl, TtsServiceConfig::defaultBaseUrl(TtsServiceKind::Piper));
    QCOMPARE(config.model, TtsServiceConfig::defaultModel(TtsServiceKind::Piper));
    QCOMPARE(spy.count(), 1);
}

void TestTtsRegistry::removeDeletesServiceAndFixesActiveId() {
    TtsRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    // The id is captured by value: add() returns a reference into the
    // services vector, which a later add() reallocates.
    const QUuid firstId = registry.add(QStringLiteral("a"), TtsServiceKind::OpenAiApi).id;
    registry.add(QStringLiteral("b"), TtsServiceKind::Groq);
    QCOMPARE(registry.activeServiceId(), firstId.toString(QUuid::WithoutBraces));
    QVERIFY(registry.remove(firstId));
    QCOMPARE(registry.activeServiceId(),
             registry.services().first().id.toString(QUuid::WithoutBraces));
}

void TestTtsRegistry::enabledFiltersDisabled() {
    TtsRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    auto &enabled = registry.add(QStringLiteral("on"), TtsServiceKind::OpenAiTts);
    auto &disabled = registry.add(QStringLiteral("off"), TtsServiceKind::Groq);
    disabled.enabled = false;
    const auto enabledServices = registry.enabled();
    QCOMPARE(enabledServices.size(), 1);
    QCOMPARE(enabledServices.first().id, enabled.id);
}

void TestTtsRegistry::settingsRoundTrip() {
    {
        TtsRegistry registry;
        QSettings s = settings();
        registry.loadSettings(s);
        auto &config = registry.add(QStringLiteral("Remote"), TtsServiceKind::Mistral);
        config.apiKey = QStringLiteral("key");
        config.voice = QStringLiteral("leonie");
        registry.save();
    }
    TtsRegistry restored;
    QSettings s = settings();
    restored.loadSettings(s);
    QCOMPARE(restored.services().size(), 1);
    QCOMPARE(restored.services().first().name, QStringLiteral("Remote"));
    QCOMPARE(restored.services().first().kind, TtsServiceKind::Mistral);
    QCOMPARE(restored.services().first().apiKey, QStringLiteral("key"));
    QCOMPARE(restored.services().first().voice, QStringLiteral("leonie"));
}

void TestTtsRegistry::outputDevicePersists() {
    {
        TtsRegistry registry;
        QSettings s = settings();
        registry.loadSettings(s);
        registry.setOutputDevice(QStringLiteral("alsa:hw1"));
        QCOMPARE(registry.outputDevice(), QStringLiteral("alsa:hw1"));
    }
    TtsRegistry restored;
    QSettings s = settings();
    restored.loadSettings(s);
    QCOMPARE(restored.outputDevice(), QStringLiteral("alsa:hw1"));
}

void TestTtsRegistry::activeServiceIdPersists() {
    TtsRegistry registry;
    QSettings s = settings();
    registry.loadSettings(s);
    const auto id = registry.add(QStringLiteral("x"), TtsServiceKind::Piper).id;
    registry.setActiveServiceId(id.toString(QUuid::WithoutBraces));
    QCOMPARE(registry.activeServiceId(), id.toString(QUuid::WithoutBraces));

    TtsRegistry restored;
    QSettings s2 = settings();
    restored.loadSettings(s2);
    QCOMPARE(restored.activeServiceId(), id.toString(QUuid::WithoutBraces));
}

void TestTtsRegistry::speakResponsesPersists() {
    QVERIFY(!TtsRegistry().speakResponses());
    {
        TtsRegistry registry;
        QSettings s = settings();
        registry.loadSettings(s);
        registry.setSpeakResponses(true);
        QVERIFY(registry.speakResponses());
    }
    TtsRegistry restored;
    QSettings s = settings();
    restored.loadSettings(s);
    QVERIFY(restored.speakResponses());
}

void TestTtsRegistry::saveAfterSettingsObjectDestroyed() {
    // Regression (#60): loadSettings() used to keep a pointer to the
    // caller's QSettings. MainWindow passes a stack-local object, so a
    // save() after it went out of scope dereferenced a dangling pointer
    // and crashed.
    {
        QSettings s = settings();
        s.setValue(QStringLiteral("tts/services"), QStringLiteral("[]"));
    }
    TtsRegistry registry;
    {
        QSettings s = settings();
        registry.loadSettings(s);
    }
    registry.add(QStringLiteral("Added after load"), TtsServiceKind::Piper);

    TtsRegistry restored;
    QSettings s2 = settings();
    restored.loadSettings(s2);
    QCOMPARE(restored.services().size(), 1);
    QCOMPARE(restored.services().first().name, QStringLiteral("Added after load"));
}

QTEST_GUILESS_MAIN(TestTtsRegistry)
#include "tst_ttsregistry.moc"
