#include "core/backendregistry.h"

#include <QSettings>
#include <QSignalSpy>
#include <QtTest>

using namespace ocapi;

class TestBackendRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void addAppendsBackendWithDefaults();
    void addEmitsChanged();
    void removeExistingBackendReturnsTrue();
    void removeUnknownBackendReturnsFalse();
    void findReturnsMatchingBackend();
    void findByStringParsesUuid();
    void findReturnsNullForUnknownId();
    void enabledFiltersDisabledBackends();
    void saveAndLoadRoundTripsAllBackends();
    void loadAppliesDefaultsForMissingFields();
    void loadHandlesCorruptSettings();

private:
    QString m_settingsPath;
};

void TestBackendRegistry::initTestCase()
{
    // Isolate QSettings so tests never touch the real user configuration.
    m_settingsPath = QDir::tempPath() + QStringLiteral("/ocapi-test-") + QUuid::createUuid().toString();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsPath);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    settings.clear();
}

void TestBackendRegistry::cleanupTestCase()
{
    QSettings settings;
    settings.clear();
    QDir(m_settingsPath).removeRecursively();
}

void TestBackendRegistry::addAppendsBackendWithDefaults()
{
    BackendRegistry registry;
    auto& config = registry.add(QStringLiteral("Local Ollama"), BackendKind::Ollama);
    QCOMPARE(registry.backends().size(), 1);
    QCOMPARE(config.name, QStringLiteral("Local Ollama"));
    QCOMPARE(config.kind, BackendKind::Ollama);
    QCOMPARE(config.baseUrl, BackendConfig::defaultBaseUrl(BackendKind::Ollama));
    QVERIFY(config.enabled);
    QVERIFY(!config.id.isNull());
}

void TestBackendRegistry::addEmitsChanged()
{
    BackendRegistry registry;
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    registry.add(QStringLiteral("a"), BackendKind::OpenAi);
    QCOMPARE(spy.count(), 1);
}

void TestBackendRegistry::removeExistingBackendReturnsTrue()
{
    BackendRegistry registry;
    auto& config = registry.add(QStringLiteral("to remove"), BackendKind::Gemini);
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    QVERIFY(registry.remove(config.id));
    QVERIFY(registry.backends().isEmpty());
    QCOMPARE(spy.count(), 1);
}

void TestBackendRegistry::removeUnknownBackendReturnsFalse()
{
    BackendRegistry registry;
    registry.add(QStringLiteral("stay"), BackendKind::Mistral);
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    QVERIFY(!registry.remove(QUuid::createUuid()));
    QCOMPARE(registry.backends().size(), 1);
    QCOMPARE(spy.count(), 0);
}

void TestBackendRegistry::findReturnsMatchingBackend()
{
    BackendRegistry registry;
    auto& added = registry.add(QStringLiteral("find me"), BackendKind::Anthropic);
    BackendConfig* found = registry.find(added.id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->name, QStringLiteral("find me"));
}

void TestBackendRegistry::findByStringParsesUuid()
{
    BackendRegistry registry;
    auto& added = registry.add(QStringLiteral("by string"), BackendKind::Vllm);
    BackendConfig* found = registry.find(added.id.toString());
    QVERIFY(found != nullptr);
    QCOMPARE(found->id, added.id);
}

void TestBackendRegistry::findReturnsNullForUnknownId()
{
    BackendRegistry registry;
    registry.add(QStringLiteral("some"), BackendKind::Ollama);
    QVERIFY(registry.find(QUuid::createUuid()) == nullptr);
    QVERIFY(registry.find(QStringLiteral("not-a-uuid")) == nullptr);
}

void TestBackendRegistry::enabledFiltersDisabledBackends()
{
    BackendRegistry registry;
    auto& enabled1 = registry.add(QStringLiteral("on1"), BackendKind::Ollama);
    auto& disabled = registry.add(QStringLiteral("off"), BackendKind::OpenAi);
    auto& enabled2 = registry.add(QStringLiteral("on2"), BackendKind::Gemini);
    disabled.enabled = false;

    const auto result = registry.enabled();
    QCOMPARE(result.size(), 2);
    const auto ids = [](const QVector<BackendConfig>& list) {
        QList<QUuid> result;
        for (const auto& c : list)
            result.append(c.id);
        return result;
    };
    QVERIFY(ids(result).contains(enabled1.id));
    QVERIFY(ids(result).contains(enabled2.id));
    QVERIFY(!ids(result).contains(disabled.id));
}

void TestBackendRegistry::saveAndLoadRoundTripsAllBackends()
{
    {
        BackendRegistry registry;
        auto& a = registry.add(QStringLiteral("Ollama local"), BackendKind::Ollama);
        a.apiKey = QStringLiteral("secret-key");
        a.defaultModel = QStringLiteral("llama3.2");
        a.baseUrl = QStringLiteral("http://192.168.1.10:11434");

        auto& b = registry.add(QStringLiteral("Cloud"), BackendKind::Anthropic);
        b.enabled = false;
        b.apiKey = QStringLiteral("sk-ant-xxx");
        b.defaultModel = QStringLiteral("claude-sonnet-4-5");

        registry.save();
    }

    {
        BackendRegistry registry;
        registry.load();
        QCOMPARE(registry.backends().size(), 2);

        const auto ollama = std::find_if(registry.backends().begin(), registry.backends().end(),
                                         [](const BackendConfig& c) { return c.kind == BackendKind::Ollama; });
        QVERIFY(ollama != registry.backends().end());
        QCOMPARE(ollama->name, QStringLiteral("Ollama local"));
        QCOMPARE(ollama->apiKey, QStringLiteral("secret-key"));
        QCOMPARE(ollama->defaultModel, QStringLiteral("llama3.2"));
        QCOMPARE(ollama->baseUrl, QStringLiteral("http://192.168.1.10:11434"));
        QVERIFY(ollama->enabled);

        const auto anthropic = std::find_if(registry.backends().begin(), registry.backends().end(),
                                            [](const BackendConfig& c) { return c.kind == BackendKind::Anthropic; });
        QVERIFY(anthropic != registry.backends().end());
        QCOMPARE(anthropic->name, QStringLiteral("Cloud"));
        QVERIFY(!anthropic->enabled);
        QCOMPARE(anthropic->apiKey, QStringLiteral("sk-ant-xxx"));
        QCOMPARE(anthropic->defaultModel, QStringLiteral("claude-sonnet-4-5"));
    }
}

void TestBackendRegistry::loadAppliesDefaultsForMissingFields()
{
    // Write a minimal entry without baseUrl/enabled to verify defaults kick in.
    {
        QSettings settings;
        settings.clear();
        settings.beginGroup(QStringLiteral("backends"));
        settings.beginWriteArray(QStringLiteral("count"));
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("id"), QUuid::createUuid().toString());
        settings.setValue(QStringLiteral("name"), QStringLiteral("minimal"));
        settings.setValue(QStringLiteral("kind"), QStringLiteral("gemini"));
        settings.endArray();
        settings.endGroup();
    }

    BackendRegistry registry;
    registry.load();
    QCOMPARE(registry.backends().size(), 1);
    const auto& config = registry.backends().constFirst();
    QCOMPARE(config.kind, BackendKind::Gemini);
    QCOMPARE(config.baseUrl, BackendConfig::defaultBaseUrl(BackendKind::Gemini));
    QVERIFY(config.enabled);
}

void TestBackendRegistry::loadHandlesCorruptSettings()
{
    {
        QSettings settings;
        settings.clear();
        settings.beginGroup(QStringLiteral("backends"));
        settings.beginWriteArray(QStringLiteral("count"));
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("id"), QStringLiteral("not-a-valid-uuid"));
        settings.setValue(QStringLiteral("name"), QStringLiteral("corrupt"));
        settings.setValue(QStringLiteral("kind"), QStringLiteral("unknown-kind"));
        settings.setValue(QStringLiteral("baseUrl"), QStringLiteral(""));
        settings.endArray();
        settings.endGroup();
    }

    BackendRegistry registry;
    registry.load();
    QCOMPARE(registry.backends().size(), 1);
    const auto& config = registry.backends().constFirst();
    QVERIFY(!config.id.isNull()); // invalid UUID replaced with a fresh one
    QCOMPARE(config.kind, BackendKind::Ollama); // unknown kind falls back to Ollama
    QCOMPARE(config.baseUrl, BackendConfig::defaultBaseUrl(BackendKind::Ollama));
}

QTEST_GUILESS_MAIN(TestBackendRegistry)
#include "tst_backendregistry.moc"
