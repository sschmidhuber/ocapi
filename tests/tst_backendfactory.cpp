#include "backends/anthropicbackend.h"
#include "backends/backendfactory.h"
#include "backends/geminibackend.h"
#include "backends/ollamabackend.h"
#include "backends/openaiapibackend.h"
#include "core/backendconfig.h"

#include <QtTest>

using namespace okapi;

class TestBackendFactory : public QObject {
    Q_OBJECT

private slots:
    void createsCorrectBackendTypeForEachKind();
    void createdBackendCarriesConfig();
};

void TestBackendFactory::createsCorrectBackendTypeForEachKind() {
    struct Case {
        BackendKind kind;
        const char *typeName;
    };
    const QList<Case> cases{
        {.kind = BackendKind::Ollama, .typeName = "okapi::OllamaBackend"},
        {.kind = BackendKind::LlamaCpp, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Vllm, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::OpenAi, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Mistral, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Zai, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Qwen, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::DeepSeek, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Groq, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::OpenRouter, .typeName = "okapi::OpenAiApiBackend"},
        {.kind = BackendKind::Anthropic, .typeName = "okapi::AnthropicBackend"},
        {.kind = BackendKind::Gemini, .typeName = "okapi::GeminiBackend"},
    };

    for (const auto &c : cases) {
        BackendConfig config;
        config.kind = c.kind;
        auto backend = createBackend(config);
        QVERIFY2(backend != nullptr, BackendConfig::kindName(c.kind).toUtf8().constData());
        QCOMPARE(backend->metaObject()->className(), QLatin1String(c.typeName));
    }
}

void TestBackendFactory::createdBackendCarriesConfig() {
    BackendConfig config;
    config.name = QStringLiteral("factory test");
    config.kind = BackendKind::Anthropic;
    config.apiKey = QStringLiteral("sk-ant-test");
    config.baseUrl = QStringLiteral("https://api.anthropic.com/v1");

    auto backend = createBackend(config);
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->config().name, QStringLiteral("factory test"));
    QCOMPARE(backend->config().kind, BackendKind::Anthropic);
    QCOMPARE(backend->config().apiKey, QStringLiteral("sk-ant-test"));
    QCOMPARE(backend->config().baseUrl, QStringLiteral("https://api.anthropic.com/v1"));
}

QTEST_GUILESS_MAIN(TestBackendFactory)
#include "tst_backendfactory.moc"
