#include "core/sttconfig.h"

#include <QtTest>

using namespace ocapi;

class TestSttConfig : public QObject {
    Q_OBJECT

private slots:
    void defaults();
    void kindNamesAndKeys();
    void kindRoundTrip();
    void defaultUrlsAndModels();
    void cloudAndApiKey();
    void effectiveBaseUrlFallsBackToDefault();
    void transcriptionPathPerKind();
    void jsonRoundTrip();
};

void TestSttConfig::defaults() {
    SttServiceConfig config;
    QVERIFY(!config.id.isNull());
    QVERIFY(config.enabled);
    QCOMPARE(config.kind, SttServiceKind::OpenAiApi);
    QVERIFY(config.name.isEmpty());
}

void TestSttConfig::kindNamesAndKeys() {
    QCOMPARE(SttServiceConfig::kindName(SttServiceKind::Groq), QStringLiteral("Groq"));
    QCOMPARE(SttServiceConfig::kindKey(SttServiceKind::WhisperCpp), QStringLiteral("whispercpp"));
    QCOMPARE(SttServiceConfig::allKinds().size(), 5);
}

void TestSttConfig::kindRoundTrip() {
    for (auto kind : SttServiceConfig::allKinds())
        QCOMPARE(SttServiceConfig::kindFromString(SttServiceConfig::kindKey(kind)), kind);
}

void TestSttConfig::defaultUrlsAndModels() {
    QVERIFY(SttServiceConfig::defaultBaseUrl(SttServiceKind::OpenAiApi).contains("openai.com"));
    QVERIFY(!SttServiceConfig::defaultModel(SttServiceKind::Groq).isEmpty());
}

void TestSttConfig::cloudAndApiKey() {
    QVERIFY(SttServiceConfig::isCloud(SttServiceKind::OpenAiApi));
    QVERIFY(SttServiceConfig::requiresApiKey(SttServiceKind::Mistral));
    QVERIFY(!SttServiceConfig::isCloud(SttServiceKind::WhisperApi));
    QVERIFY(!SttServiceConfig::requiresApiKey(SttServiceKind::WhisperCpp));
}

void TestSttConfig::effectiveBaseUrlFallsBackToDefault() {
    SttServiceConfig config;
    config.kind = SttServiceKind::Groq;
    config.baseUrl = QStringLiteral("  ");
    QCOMPARE(config.effectiveBaseUrl(), SttServiceConfig::defaultBaseUrl(SttServiceKind::Groq));
}

void TestSttConfig::transcriptionPathPerKind() {
    SttServiceConfig config;
    config.kind = SttServiceKind::WhisperCpp;
    QCOMPARE(config.transcriptionPath(), QStringLiteral("/inference"));
    config.kind = SttServiceKind::OpenAiApi;
    QCOMPARE(config.transcriptionPath(), QStringLiteral("/audio/transcriptions"));
}

void TestSttConfig::jsonRoundTrip() {
    SttServiceConfig config;
    config.name = QStringLiteral("Local whisper");
    config.kind = SttServiceKind::WhisperApi;
    config.baseUrl = QStringLiteral("http://192.168.1.10:8000/v1");
    config.apiKey = QStringLiteral("secret");
    config.model = QStringLiteral("whisper-1");
    config.language = QStringLiteral("de");
    config.enabled = false;

    const SttServiceConfig restored = SttServiceConfig::fromJson(config.toJson());
    QCOMPARE(restored.id, config.id);
    QCOMPARE(restored.name, config.name);
    QCOMPARE(restored.kind, config.kind);
    QCOMPARE(restored.baseUrl, config.baseUrl);
    QCOMPARE(restored.apiKey, config.apiKey);
    QCOMPARE(restored.model, config.model);
    QCOMPARE(restored.language, config.language);
    QCOMPARE(restored.enabled, false);
}

QTEST_GUILESS_MAIN(TestSttConfig)
#include "tst_sttconfig.moc"
