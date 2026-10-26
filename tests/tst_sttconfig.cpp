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
    void chunkDurationDefaultsAndClamps();
    void streamingKindsAreFlagged();
    void newKindRoundTrip();
    void jsonRoundTrip();
    void selectableKindsSkipNonStreamingVariants();
    void llmProviderMapping();
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
    QCOMPARE(SttServiceConfig::kindKey(SttServiceKind::OpenAiRealtime),
             QStringLiteral("openairealtime"));
    QCOMPARE(SttServiceConfig::kindKey(SttServiceKind::MistralRealtime),
             QStringLiteral("mistralrealtime"));
    QCOMPARE(SttServiceConfig::allKinds().size(), 7);
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

void TestSttConfig::chunkDurationDefaultsAndClamps() {
    SttServiceConfig config;
    QCOMPARE(config.chunkDurationMs, 2500);
    QCOMPARE(config.effectiveChunkDurationMs(), 2500);
    config.chunkDurationMs = 500;
    QCOMPARE(config.effectiveChunkDurationMs(), 1000);
    config.chunkDurationMs = 60000;
    QCOMPARE(config.effectiveChunkDurationMs(), 10000);
}

void TestSttConfig::streamingKindsAreFlagged() {
    QVERIFY(SttServiceConfig::isStreaming(SttServiceKind::OpenAiRealtime));
    QVERIFY(SttServiceConfig::isStreaming(SttServiceKind::MistralRealtime));
    QVERIFY(!SttServiceConfig::isStreaming(SttServiceKind::OpenAiApi));
    QVERIFY(!SttServiceConfig::isStreaming(SttServiceKind::Groq));
    QVERIFY(!SttServiceConfig::isStreaming(SttServiceKind::Mistral));
    QVERIFY(!SttServiceConfig::isStreaming(SttServiceKind::WhisperApi));
    QVERIFY(!SttServiceConfig::isStreaming(SttServiceKind::WhisperCpp));

    SttServiceConfig config;
    config.kind = SttServiceKind::MistralRealtime;
    QVERIFY(config.isStreaming());
    QVERIFY(SttServiceConfig::isCloud(SttServiceKind::OpenAiRealtime));
    QVERIFY(SttServiceConfig::requiresApiKey(SttServiceKind::MistralRealtime));
    QCOMPARE(SttServiceConfig::defaultModel(SttServiceKind::OpenAiRealtime),
             QStringLiteral("gpt-live-transcribe"));
    QCOMPARE(SttServiceConfig::defaultModel(SttServiceKind::MistralRealtime),
             QStringLiteral("voxtral-mini-transcribe-realtime-2602"));
    QVERIFY(SttServiceConfig::defaultBaseUrl(SttServiceKind::MistralRealtime)
                .startsWith(QStringLiteral("wss://")));
}

void TestSttConfig::newKindRoundTrip() {
    for (auto kind : {SttServiceKind::OpenAiRealtime, SttServiceKind::MistralRealtime}) {
        QCOMPARE(SttServiceConfig::kindFromString(SttServiceConfig::kindKey(kind)), kind);
    }
}

void TestSttConfig::jsonRoundTrip() {
    SttServiceConfig config;
    config.name = QStringLiteral("Local whisper");
    config.kind = SttServiceKind::WhisperApi;
    config.baseUrl = QStringLiteral("http://192.168.1.10:8000/v1");
    config.apiKey = QStringLiteral("secret");
    config.model = QStringLiteral("whisper-1");
    config.language = QStringLiteral("de");
    config.chunkDurationMs = 3000;
    config.enabled = false;

    const SttServiceConfig restored = SttServiceConfig::fromJson(config.toJson());
    QCOMPARE(restored.id, config.id);
    QCOMPARE(restored.name, config.name);
    QCOMPARE(restored.kind, config.kind);
    QCOMPARE(restored.baseUrl, config.baseUrl);
    QCOMPARE(restored.apiKey, config.apiKey);
    QCOMPARE(restored.model, config.model);
    QCOMPARE(restored.language, config.language);
    QCOMPARE(restored.chunkDurationMs, 3000);
    QCOMPARE(restored.enabled, false);
}

void TestSttConfig::selectableKindsSkipNonStreamingVariants() {
    // Providers with a streaming API must not offer their non-streaming
    // (chunked HTTP) variant for new services; self-hosted servers have no
    // streaming API and keep all their kinds.
    const auto selectable = SttServiceConfig::selectableKinds();
    QVERIFY(!selectable.contains(SttServiceKind::OpenAiApi));
    QVERIFY(!selectable.contains(SttServiceKind::Mistral));
    QVERIFY(selectable.contains(SttServiceKind::OpenAiRealtime));
    QVERIFY(selectable.contains(SttServiceKind::MistralRealtime));
    QVERIFY(selectable.contains(SttServiceKind::Groq));
    QVERIFY(selectable.contains(SttServiceKind::WhisperApi));
    QVERIFY(selectable.contains(SttServiceKind::WhisperCpp));
}

void TestSttConfig::llmProviderMapping() {
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::OpenAiApi), BackendKind::OpenAi);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::OpenAiRealtime),
             BackendKind::OpenAi);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::Groq), BackendKind::Groq);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::Mistral), BackendKind::Mistral);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::MistralRealtime),
             BackendKind::Mistral);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::WhisperApi), std::nullopt);
    QCOMPARE(SttServiceConfig::llmProviderKind(SttServiceKind::WhisperCpp), std::nullopt);
}

QTEST_GUILESS_MAIN(TestSttConfig)
#include "tst_sttconfig.moc"
