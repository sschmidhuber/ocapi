#include "core/backendconfig.h"

#include <QtTest>

using namespace ocapi;

class TestBackendConfig : public QObject {
    Q_OBJECT

private slots:
    void kindNameIsUniqueAndNonEmpty();
    void kindDescriptionIsNonEmpty();
    void defaultBaseUrlIsValidHttpUrl();
    void defaultConstructedConfigIsEnabledWithUniqueId();
    void everyKindHasMatchingRoundTripName();
    void cloudKindsDoNotRequireBaseUrl();
    void effectiveBaseUrlPrefersUserValue();
    void allKindsAreKnown();
};

void TestBackendConfig::kindNameIsUniqueAndNonEmpty() {
    const QList<BackendKind> kinds{
        BackendKind::Ollama,    BackendKind::LlamaCpp, BackendKind::Vllm,    BackendKind::OpenAi,
        BackendKind::Anthropic, BackendKind::Gemini,   BackendKind::Mistral,
    };
    QSet<QString> names;
    for (const auto kind : kinds) {
        const QString name = BackendConfig::kindName(kind);
        QVERIFY2(!name.isEmpty(), "kindName must not be empty");
        QVERIFY2(!names.contains(name),
                 qPrintable(QStringLiteral("duplicate kindName: %1").arg(name)));
        names.insert(name);
    }
    QCOMPARE(names.size(), kinds.size());
}

void TestBackendConfig::kindDescriptionIsNonEmpty() {
    const QList<BackendKind> kinds{
        BackendKind::Ollama,    BackendKind::LlamaCpp, BackendKind::Vllm,    BackendKind::OpenAi,
        BackendKind::Anthropic, BackendKind::Gemini,   BackendKind::Mistral,
    };
    for (const auto kind : kinds)
        QVERIFY2(
            !BackendConfig::kindDescription(kind).isEmpty(),
            qPrintable(
                QStringLiteral("kindDescription empty for %1").arg(BackendConfig::kindName(kind))));
}

void TestBackendConfig::defaultBaseUrlIsValidHttpUrl() {
    const QList<BackendKind> kinds{
        BackendKind::Ollama,    BackendKind::LlamaCpp, BackendKind::Vllm,    BackendKind::OpenAi,
        BackendKind::Anthropic, BackendKind::Gemini,   BackendKind::Mistral,
    };
    for (const auto kind : kinds) {
        const QUrl url(BackendConfig::defaultBaseUrl(kind));
        QVERIFY2(url.isValid(), qPrintable(BackendConfig::defaultBaseUrl(kind)));
        QVERIFY2(url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"),
                 qPrintable(BackendConfig::defaultBaseUrl(kind)));
        QVERIFY2(!url.host().isEmpty(), qPrintable(BackendConfig::defaultBaseUrl(kind)));
    }
}

void TestBackendConfig::defaultConstructedConfigIsEnabledWithUniqueId() {
    BackendConfig a;
    BackendConfig b;
    QVERIFY(a.enabled);
    QVERIFY(!a.id.isNull());
    QVERIFY(a.id != b.id);
    QCOMPARE(a.kindName(), BackendConfig::kindName(a.kind));
}

void TestBackendConfig::everyKindHasMatchingRoundTripName() {
    // kindName() is the human-readable label; make sure the expected
    // well-known labels are stable so UI and settings stay compatible.
    QCOMPARE(BackendConfig::kindName(BackendKind::Ollama), QStringLiteral("Ollama"));
    QCOMPARE(BackendConfig::kindName(BackendKind::LlamaCpp), QStringLiteral("llama.cpp"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Vllm), QStringLiteral("vLLM"));
    QCOMPARE(BackendConfig::kindName(BackendKind::OpenAi), QStringLiteral("OpenAI"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Anthropic), QStringLiteral("Anthropic"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Gemini), QStringLiteral("Google Gemini"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Mistral), QStringLiteral("Mistral AI"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Zai), QStringLiteral("Z.ai (GLM)"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Qwen), QStringLiteral("Alibaba Qwen"));
    QCOMPARE(BackendConfig::kindName(BackendKind::DeepSeek), QStringLiteral("DeepSeek"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Groq), QStringLiteral("Groq"));
}

void TestBackendConfig::cloudKindsDoNotRequireBaseUrl() {
    for (const auto kind : BackendConfig::allKinds()) {
        BackendConfig config;
        config.kind = kind;
        config.baseUrl.clear();
        // With an empty baseUrl every kind resolves to its provider default,
        // so cloud providers never require a user-entered URL.
        QCOMPARE(config.effectiveBaseUrl(), BackendConfig::defaultBaseUrl(kind));
    }
    QVERIFY(BackendConfig::isCloud(BackendKind::OpenAi));
    QVERIFY(BackendConfig::isCloud(BackendKind::Zai));
    QVERIFY(BackendConfig::isCloud(BackendKind::Qwen));
    QVERIFY(!BackendConfig::isCloud(BackendKind::Ollama));
    QVERIFY(!BackendConfig::isCloud(BackendKind::LlamaCpp));
    QVERIFY(!BackendConfig::isCloud(BackendKind::Vllm));
}

void TestBackendConfig::effectiveBaseUrlPrefersUserValue() {
    BackendConfig config;
    config.kind = BackendKind::OpenAi;
    config.baseUrl = QStringLiteral("https://example.com/v1");
    QCOMPARE(config.effectiveBaseUrl(), QStringLiteral("https://example.com/v1"));
    config.baseUrl = QStringLiteral("  https://api.openai.com/v1  ");
    QCOMPARE(config.effectiveBaseUrl(), QStringLiteral("https://api.openai.com/v1"));
}

void TestBackendConfig::allKindsAreKnown() {
    const auto kinds = BackendConfig::allKinds();
    QVERIFY(kinds.size() >= 11);
    for (const auto kind : kinds) {
        QVERIFY2(!BackendConfig::kindName(kind).isEmpty(), "every kind has a name");
        QVERIFY2(!BackendConfig::defaultBaseUrl(kind).isEmpty(), "every kind has a default URL");
    }
}

QTEST_GUILESS_MAIN(TestBackendConfig)
#include "tst_backendconfig.moc"
