#include "core/backendconfig.h"

#include <QtTest>

using namespace ocapi;

class TestBackendConfig : public QObject
{
    Q_OBJECT

private slots:
    void kindNameIsUniqueAndNonEmpty();
    void kindDescriptionIsNonEmpty();
    void defaultBaseUrlIsValidHttpUrl();
    void defaultConstructedConfigIsEnabledWithUniqueId();
    void everyKindHasMatchingRoundTripName();
};

void TestBackendConfig::kindNameIsUniqueAndNonEmpty()
{
    const QList<BackendKind> kinds{
        BackendKind::Ollama, BackendKind::LlamaCpp, BackendKind::Vllm,
        BackendKind::OpenAi, BackendKind::Anthropic, BackendKind::Gemini,
        BackendKind::Mistral,
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

void TestBackendConfig::kindDescriptionIsNonEmpty()
{
    const QList<BackendKind> kinds{
        BackendKind::Ollama, BackendKind::LlamaCpp, BackendKind::Vllm,
        BackendKind::OpenAi, BackendKind::Anthropic, BackendKind::Gemini,
        BackendKind::Mistral,
    };
    for (const auto kind : kinds)
        QVERIFY2(!BackendConfig::kindDescription(kind).isEmpty(),
                 qPrintable(QStringLiteral("kindDescription empty for %1")
                                .arg(BackendConfig::kindName(kind))));
}

void TestBackendConfig::defaultBaseUrlIsValidHttpUrl()
{
    const QList<BackendKind> kinds{
        BackendKind::Ollama, BackendKind::LlamaCpp, BackendKind::Vllm,
        BackendKind::OpenAi, BackendKind::Anthropic, BackendKind::Gemini,
        BackendKind::Mistral,
    };
    for (const auto kind : kinds) {
        const QUrl url(BackendConfig::defaultBaseUrl(kind));
        QVERIFY2(url.isValid(), qPrintable(BackendConfig::defaultBaseUrl(kind)));
        QVERIFY2(url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"),
                 qPrintable(BackendConfig::defaultBaseUrl(kind)));
        QVERIFY2(!url.host().isEmpty(), qPrintable(BackendConfig::defaultBaseUrl(kind)));
    }
}

void TestBackendConfig::defaultConstructedConfigIsEnabledWithUniqueId()
{
    BackendConfig a;
    BackendConfig b;
    QVERIFY(a.enabled);
    QVERIFY(!a.id.isNull());
    QVERIFY(a.id != b.id);
    QCOMPARE(a.kindName(), BackendConfig::kindName(a.kind));
}

void TestBackendConfig::everyKindHasMatchingRoundTripName()
{
    // kindName() is the human-readable label; make sure the expected
    // well-known labels are stable so UI and settings stay compatible.
    QCOMPARE(BackendConfig::kindName(BackendKind::Ollama), QStringLiteral("Ollama"));
    QCOMPARE(BackendConfig::kindName(BackendKind::LlamaCpp), QStringLiteral("llama.cpp"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Vllm), QStringLiteral("vLLM"));
    QCOMPARE(BackendConfig::kindName(BackendKind::OpenAi), QStringLiteral("OpenAI"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Anthropic), QStringLiteral("Anthropic"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Gemini), QStringLiteral("Google Gemini"));
    QCOMPARE(BackendConfig::kindName(BackendKind::Mistral), QStringLiteral("Mistral AI"));
}

QTEST_GUILESS_MAIN(TestBackendConfig)
#include "tst_backendconfig.moc"
