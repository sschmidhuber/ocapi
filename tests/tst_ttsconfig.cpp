#include "core/ttsconfig.h"

#include <QtTest>

using namespace ocapi;

class TestTtsConfig : public QObject {
    Q_OBJECT

private slots:
    void defaults();
    void kindNamesAndKeys();
    void kindRoundTrip();
    void defaultUrlsModelsVoices();
    void cloudAndApiKey();
    void effectiveBaseUrlFallsBackToDefault();
    void responseFormatPerKind();
    void speechPathPerKind();
    void jsonRoundTrip();
    void llmProviderMapping();
};

void TestTtsConfig::defaults() {
    TtsServiceConfig config;
    QVERIFY(!config.id.isNull());
    QVERIFY(config.enabled);
    QCOMPARE(config.kind, TtsServiceKind::OpenAiApi);
    QVERIFY(config.name.isEmpty());
}

void TestTtsConfig::kindNamesAndKeys() {
    QCOMPARE(TtsServiceConfig::kindName(TtsServiceKind::ElevenLabs), QStringLiteral("ElevenLabs"));
    QCOMPARE(TtsServiceConfig::kindKey(TtsServiceKind::Piper), QStringLiteral("piper"));
    QCOMPARE(TtsServiceConfig::kindKey(TtsServiceKind::OpenAiTts), QStringLiteral("openaitts"));
    QCOMPARE(TtsServiceConfig::allKinds().size(), 6);
}

void TestTtsConfig::kindRoundTrip() {
    for (auto kind : TtsServiceConfig::allKinds())
        QCOMPARE(TtsServiceConfig::kindFromString(TtsServiceConfig::kindKey(kind)), kind);
    QCOMPARE(TtsServiceConfig::kindFromString(QStringLiteral("bogus")), TtsServiceKind::OpenAiTts);
}

void TestTtsConfig::defaultUrlsModelsVoices() {
    QVERIFY(TtsServiceConfig::defaultBaseUrl(TtsServiceKind::OpenAiApi).contains("openai.com"));
    QVERIFY(TtsServiceConfig::defaultBaseUrl(TtsServiceKind::ElevenLabs).contains("elevenlabs.io"));
    QVERIFY(!TtsServiceConfig::defaultModel(TtsServiceKind::Piper).isEmpty());
    QVERIFY(!TtsServiceConfig::defaultVoice(TtsServiceKind::OpenAiApi).isEmpty());
    QVERIFY(TtsServiceConfig::defaultVoice(TtsServiceKind::Piper).isEmpty());
    // Mistral's speech API serves Voxtral TTS; the retired "mistral-tts-latest"
    // model id is rejected with 400 invalid_model, and voices are passed as
    // "voice_id" slugs (e.g. gb_jane_neutral), not OpenAI-style names.
    QCOMPARE(TtsServiceConfig::defaultModel(TtsServiceKind::Mistral),
             QStringLiteral("voxtral-mini-tts-2603"));
    QCOMPARE(TtsServiceConfig::defaultVoice(TtsServiceKind::Mistral),
             QStringLiteral("gb_jane_neutral"));
}

void TestTtsConfig::cloudAndApiKey() {
    QVERIFY(TtsServiceConfig::isCloud(TtsServiceKind::OpenAiApi));
    QVERIFY(TtsServiceConfig::requiresApiKey(TtsServiceKind::ElevenLabs));
    QVERIFY(!TtsServiceConfig::isCloud(TtsServiceKind::OpenAiTts));
    QVERIFY(!TtsServiceConfig::requiresApiKey(TtsServiceKind::Piper));
}

void TestTtsConfig::effectiveBaseUrlFallsBackToDefault() {
    TtsServiceConfig config;
    config.kind = TtsServiceKind::Groq;
    config.baseUrl = QStringLiteral("  ");
    QCOMPARE(config.effectiveBaseUrl(), TtsServiceConfig::defaultBaseUrl(TtsServiceKind::Groq));
}

void TestTtsConfig::responseFormatPerKind() {
    QCOMPARE(TtsServiceConfig::responseFormat(TtsServiceKind::Piper), QStringLiteral("wav"));
    QCOMPARE(TtsServiceConfig::responseFormat(TtsServiceKind::OpenAiApi), QStringLiteral("mp3"));
    QCOMPARE(TtsServiceConfig::responseMimeType(TtsServiceKind::Piper),
             QStringLiteral("audio/wav"));
    QCOMPARE(TtsServiceConfig::responseMimeType(TtsServiceKind::ElevenLabs),
             QStringLiteral("audio/mpeg"));
}

void TestTtsConfig::speechPathPerKind() {
    TtsServiceConfig config;
    config.kind = TtsServiceKind::OpenAiApi;
    QCOMPARE(config.speechPath(), QStringLiteral("/audio/speech"));
    config.kind = TtsServiceKind::Piper;
    QCOMPARE(config.speechPath(), QStringLiteral("/api/tts"));
    config.kind = TtsServiceKind::ElevenLabs;
    config.voice = QStringLiteral("voice-id-1");
    QCOMPARE(config.speechPath(), QStringLiteral("/text-to-speech/voice-id-1"));
}

void TestTtsConfig::jsonRoundTrip() {
    TtsServiceConfig config;
    config.name = QStringLiteral("Local piper");
    config.kind = TtsServiceKind::Piper;
    config.baseUrl = QStringLiteral("http://192.168.1.10:5000");
    config.apiKey = QStringLiteral("secret");
    config.model = QStringLiteral("de_DE-thorsten-medium");
    config.voice = QStringLiteral("alloy");
    config.language = QStringLiteral("de");
    config.enabled = false;

    const TtsServiceConfig restored = TtsServiceConfig::fromJson(config.toJson());
    QCOMPARE(restored.id, config.id);
    QCOMPARE(restored.name, config.name);
    QCOMPARE(restored.kind, config.kind);
    QCOMPARE(restored.baseUrl, config.baseUrl);
    QCOMPARE(restored.apiKey, config.apiKey);
    QCOMPARE(restored.model, config.model);
    QCOMPARE(restored.voice, config.voice);
    QCOMPARE(restored.language, config.language);
    QCOMPARE(restored.enabled, false);
}

void TestTtsConfig::llmProviderMapping() {
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::OpenAiApi), BackendKind::OpenAi);
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::Groq), BackendKind::Groq);
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::Mistral), BackendKind::Mistral);
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::ElevenLabs), std::nullopt);
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::OpenAiTts), std::nullopt);
    QCOMPARE(TtsServiceConfig::llmProviderKind(TtsServiceKind::Piper), std::nullopt);
}

QTEST_GUILESS_MAIN(TestTtsConfig)
#include "tst_ttsconfig.moc"
