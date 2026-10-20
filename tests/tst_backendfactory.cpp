#include "backends/anthropicbackend.h"
#include "backends/backendfactory.h"
#include "backends/geminibackend.h"
#include "backends/ollamabackend.h"
#include "backends/openaiapibackend.h"
#include "core/backendconfig.h"

#include <QtTest>

using namespace ocapi;

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
        {BackendKind::Ollama, "ocapi::OllamaBackend"},
        {BackendKind::LlamaCpp, "ocapi::OpenAiApiBackend"},
        {BackendKind::Vllm, "ocapi::OpenAiApiBackend"},
        {BackendKind::OpenAi, "ocapi::OpenAiApiBackend"},
        {BackendKind::Mistral, "ocapi::OpenAiApiBackend"},
        {BackendKind::Zai, "ocapi::OpenAiApiBackend"},
        {BackendKind::Qwen, "ocapi::OpenAiApiBackend"},
        {BackendKind::DeepSeek, "ocapi::OpenAiApiBackend"},
        {BackendKind::Groq, "ocapi::OpenAiApiBackend"},
        {BackendKind::Anthropic, "ocapi::AnthropicBackend"},
        {BackendKind::Gemini, "ocapi::GeminiBackend"},
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
