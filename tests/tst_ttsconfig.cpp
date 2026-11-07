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
    void speedDefaultsAndClamping();
    void availableVoicesPerKind();
    void speedJsonRoundTrip();
    void supportsSpeedPerKind();
    void mistralPaceTagMapping();
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
    // Mistral's speech API serves Voxtral TTS 3; voices are passed as
    // "voice_id" slugs (e.g. gb_jane_neutral), not OpenAI-style names, and
    // pace is set via inline [pace=...] tags supported by Voxtral 3 models.
    QCOMPARE(TtsServiceConfig::defaultModel(TtsServiceKind::Mistral),
             QStringLiteral("voxtral-mini-tts-3-0"));
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
    config.speed = 1.5;
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
    QCOMPARE(restored.speed, 1.5);
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

void TestTtsConfig::speedDefaultsAndClamping() {
    TtsServiceConfig config;
    QCOMPARE(config.speed, 1.0);
    QCOMPARE(config.effectiveSpeed(), 1.0);

    config.speed = 0.5;
    QCOMPARE(config.effectiveSpeed(), 0.5);
    config.speed = 2.5;
    QCOMPARE(config.effectiveSpeed(), 2.5);
    // Out-of-range values fall back to the default instead of being sent.
    config.speed = 0.1;
    QCOMPARE(config.effectiveSpeed(), 1.0);
    config.speed = 10.0;
    QCOMPARE(config.effectiveSpeed(), 1.0);
    config.speed = -1.0;
    QCOMPARE(config.effectiveSpeed(), 1.0);

    QVERIFY(TtsServiceConfig::minSpeed < TtsServiceConfig::defaultSpeed);
    QVERIFY(TtsServiceConfig::defaultSpeed < TtsServiceConfig::maxSpeed);
}

void TestTtsConfig::availableVoicesPerKind() {
    const QStringList openAiVoices = TtsServiceConfig::availableVoices(TtsServiceKind::OpenAiApi);
    QVERIFY(!openAiVoices.isEmpty());
    QVERIFY(openAiVoices.contains(TtsServiceConfig::defaultVoice(TtsServiceKind::OpenAiApi)));
    QVERIFY(
        TtsServiceConfig::availableVoices(TtsServiceKind::Groq).contains(QStringLiteral("alloy")));
    QVERIFY(TtsServiceConfig::availableVoices(TtsServiceKind::OpenAiTts)
                .contains(QStringLiteral("nova")));
    // Mistral offers its Voxtral preset voices in the dropdown; the default
    // voice must be among them.
    const QStringList mistralVoices = TtsServiceConfig::availableVoices(TtsServiceKind::Mistral);
    QVERIFY(!mistralVoices.isEmpty());
    QVERIFY(mistralVoices.contains(TtsServiceConfig::defaultVoice(TtsServiceKind::Mistral)));
    QVERIFY(mistralVoices.contains(QStringLiteral("gb_jane_neutral")));
    QVERIFY(mistralVoices.contains(QStringLiteral("voice_478")));
    // Voice is a free-form ID for these services, not a preset list.
    QVERIFY(TtsServiceConfig::availableVoices(TtsServiceKind::ElevenLabs).isEmpty());
    QVERIFY(TtsServiceConfig::availableVoices(TtsServiceKind::Piper).isEmpty());
}

void TestTtsConfig::speedJsonRoundTrip() {
    TtsServiceConfig config;
    config.speed = 0.75;
    const TtsServiceConfig restored = TtsServiceConfig::fromJson(config.toJson());
    QCOMPARE(restored.speed, 0.75);

    // Legacy settings without a "speed" key load at the default speed.
    QJsonObject legacy;
    legacy.insert(QStringLiteral("name"), QStringLiteral("legacy"));
    QCOMPARE(TtsServiceConfig::fromJson(legacy).speed, 1.0);

    // Out-of-range persisted values are normalized to the default.
    QJsonObject invalid;
    invalid.insert(QStringLiteral("speed"), 99.0);
    QCOMPARE(TtsServiceConfig::fromJson(invalid).speed, 1.0);
}

void TestTtsConfig::supportsSpeedPerKind() {
    // Mistral has no "speed" request field, but pace is configurable via
    // inline [pace=...] tags, so the slider is shown for all kinds.
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::Mistral));
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::OpenAiApi));
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::Groq));
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::ElevenLabs));
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::OpenAiTts));
    QVERIFY(TtsServiceConfig::supportsSpeed(TtsServiceKind::Piper));
}

void TestTtsConfig::mistralPaceTagMapping() {
    QCOMPARE(TtsServiceConfig::availablePaces().size(), 5);
    QCOMPARE(TtsServiceConfig::mistralPaceTag(TtsPace::VerySlow),
             QStringLiteral("[pace=very-slow]"));
    QCOMPARE(TtsServiceConfig::mistralPaceTag(TtsPace::Slow), QStringLiteral("[pace=slow]"));
    QCOMPARE(TtsServiceConfig::mistralPaceTag(TtsPace::Moderate),
             QStringLiteral("[pace=moderate]"));
    QCOMPARE(TtsServiceConfig::mistralPaceTag(TtsPace::Fast), QStringLiteral("[pace=fast]"));
    QCOMPARE(TtsServiceConfig::mistralPaceTag(TtsPace::VeryFast),
             QStringLiteral("[pace=very-fast]"));

    QCOMPARE(TtsServiceConfig::paceFromSpeed(0.25), TtsPace::VerySlow);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(0.5), TtsPace::VerySlow);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(0.75), TtsPace::Slow);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(1.0), TtsPace::Moderate);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(1.5), TtsPace::Fast);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(2.0), TtsPace::VeryFast);
    QCOMPARE(TtsServiceConfig::paceFromSpeed(4.0), TtsPace::VeryFast);

    // Round trip: speed -> pace -> speed stays on the pace's factor.
    for (auto pace : TtsServiceConfig::availablePaces())
        QCOMPARE(TtsServiceConfig::paceFromSpeed(TtsServiceConfig::speedFromPace(pace)), pace);

    QCOMPARE(TtsServiceConfig::speedLabel(1.0, TtsServiceKind::Mistral),
             QStringLiteral("moderate"));
    QCOMPARE(TtsServiceConfig::speedLabel(2.0, TtsServiceKind::Mistral),
             QStringLiteral("very fast"));
    QCOMPARE(TtsServiceConfig::speedLabel(1.5, TtsServiceKind::OpenAiApi),
             QStringLiteral("1.50\u00d7"));
}

QTEST_GUILESS_MAIN(TestTtsConfig)
#include "tst_ttsconfig.moc"
