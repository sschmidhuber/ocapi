#include "core/sttconfig.h"
#include "core/voiceinput.h"

#include <QtTest>

using namespace ocapi;

class TestVoiceInput : public QObject {
    Q_OBJECT

private slots:
    void streamingServiceUsesStreamClient();
    void nonStreamingServiceUsesHttpClient();
    void switchingServiceReplacesClient();
};

void TestVoiceInput::streamingServiceUsesStreamClient() {
    // Regression: streaming services (wss:// endpoints) used to be wired to
    // the chunked HTTP client, whose QNetworkAccessManager rejects the wss
    // scheme ("unknown protocol" dictation error).
    VoiceInput voice;
    SttServiceConfig config;
    config.kind = SttServiceKind::MistralRealtime;
    voice.setService(config);
    QVERIFY(voice.usesStreaming());

    config.kind = SttServiceKind::OpenAiRealtime;
    voice.setService(config);
    QVERIFY(voice.usesStreaming());
}

void TestVoiceInput::nonStreamingServiceUsesHttpClient() {
    VoiceInput voice;
    SttServiceConfig config;
    config.kind = SttServiceKind::Groq;
    voice.setService(config);
    QVERIFY(!voice.usesStreaming());

    config.kind = SttServiceKind::WhisperApi;
    voice.setService(config);
    QVERIFY(!voice.usesStreaming());
}

void TestVoiceInput::switchingServiceReplacesClient() {
    VoiceInput voice;
    SttServiceConfig config;
    config.kind = SttServiceKind::MistralRealtime;
    voice.setService(config);
    QVERIFY(voice.usesStreaming());

    config.kind = SttServiceKind::WhisperApi;
    voice.setService(config);
    QVERIFY(!voice.usesStreaming());

    config.kind = SttServiceKind::OpenAiRealtime;
    voice.setService(config);
    QVERIFY(voice.usesStreaming());
}

QTEST_GUILESS_MAIN(TestVoiceInput)
#include "tst_voiceinput.moc"
