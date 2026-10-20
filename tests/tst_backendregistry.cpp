#include "core/backendregistry.h"
#include "storage/chatdatabase.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestBackendRegistry : public QObject {
    Q_OBJECT

private slots:
    void addAppendsBackendWithDefaults();
    void addEmitsChanged();
    void removeExistingBackendReturnsTrue();
    void removeUnknownBackendReturnsFalse();
    void findReturnsMatchingBackend();
    void findByStringParsesUuid();
    void findReturnsNullForUnknownId();
    void enabledFiltersDisabledBackends();
    void databaseRoundTripPreservesAllFields();
    void removePersistsToDatabase();
};

void TestBackendRegistry::addAppendsBackendWithDefaults() {
    BackendRegistry registry;
    auto &config = registry.add(QStringLiteral("Local Ollama"), BackendKind::Ollama);
    QCOMPARE(registry.backends().size(), 1);
    QCOMPARE(config.name, QStringLiteral("Local Ollama"));
    QCOMPARE(config.kind, BackendKind::Ollama);
    QCOMPARE(config.baseUrl, BackendConfig::defaultBaseUrl(BackendKind::Ollama));
    QVERIFY(config.enabled);
    QVERIFY(!config.id.isNull());
}

void TestBackendRegistry::addEmitsChanged() {
    BackendRegistry registry;
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    registry.add(QStringLiteral("a"), BackendKind::OpenAi);
    QCOMPARE(spy.count(), 1);
}

void TestBackendRegistry::removeExistingBackendReturnsTrue() {
    BackendRegistry registry;
    auto &config = registry.add(QStringLiteral("to remove"), BackendKind::Gemini);
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    QVERIFY(registry.remove(config.id));
    QVERIFY(registry.backends().isEmpty());
    QCOMPARE(spy.count(), 1);
}

void TestBackendRegistry::removeUnknownBackendReturnsFalse() {
    BackendRegistry registry;
    registry.add(QStringLiteral("stay"), BackendKind::Mistral);
    QSignalSpy spy(&registry, &BackendRegistry::changed);
    QVERIFY(!registry.remove(QUuid::createUuid()));
    QCOMPARE(registry.backends().size(), 1);
    QCOMPARE(spy.count(), 0);
}

void TestBackendRegistry::findReturnsMatchingBackend() {
    BackendRegistry registry;
    auto &added = registry.add(QStringLiteral("find me"), BackendKind::Anthropic);
    BackendConfig *found = registry.find(added.id);
    QVERIFY(found != nullptr);
    QCOMPARE(found->name, QStringLiteral("find me"));
}

void TestBackendRegistry::findByStringParsesUuid() {
    BackendRegistry registry;
    auto &added = registry.add(QStringLiteral("by string"), BackendKind::Vllm);
    BackendConfig *found = registry.find(added.id.toString());
    QVERIFY(found != nullptr);
    QCOMPARE(found->id, added.id);
}

void TestBackendRegistry::findReturnsNullForUnknownId() {
    BackendRegistry registry;
    registry.add(QStringLiteral("some"), BackendKind::Ollama);
    QVERIFY(registry.find(QUuid::createUuid()) == nullptr);
    QVERIFY(registry.find(QStringLiteral("not-a-uuid")) == nullptr);
}

void TestBackendRegistry::enabledFiltersDisabledBackends() {
    BackendRegistry registry;
    auto &enabled1 = registry.add(QStringLiteral("on1"), BackendKind::Ollama);
    auto &disabled = registry.add(QStringLiteral("off"), BackendKind::OpenAi);
    auto &enabled2 = registry.add(QStringLiteral("on2"), BackendKind::Gemini);
    disabled.enabled = false;

    const auto result = registry.enabled();
    QCOMPARE(result.size(), 2);
    const auto ids = [](const QVector<BackendConfig> &list) {
        QList<QUuid> result;
        for (const auto &c : list)
            result.append(c.id);
        return result;
    };
    QVERIFY(ids(result).contains(enabled1.id));
    QVERIFY(ids(result).contains(enabled2.id));
    QVERIFY(!ids(result).contains(disabled.id));
}

void TestBackendRegistry::databaseRoundTripPreservesAllFields() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        ChatDatabase database;
        QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
        BackendRegistry registry;
        registry.attachDatabase(&database);
        auto &a = registry.add(QStringLiteral("Ollama local"), BackendKind::Ollama);
        a.apiKey = QStringLiteral("secret-key");
        a.defaultModel = QStringLiteral("llama3.2");
        a.baseUrl = QStringLiteral("http://192.168.1.10:11434");
        registry.save();
        auto &b = registry.add(QStringLiteral("DeepSeek"), BackendKind::DeepSeek);
        b.enabled = false;
        registry.save();
    }
    {
        ChatDatabase database;
        QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
        BackendRegistry registry;
        registry.load(database);
        QCOMPARE(registry.backends().size(), 2);
        const auto ollama =
            std::find_if(registry.backends().begin(), registry.backends().end(),
                         [](const BackendConfig &c) { return c.kind == BackendKind::Ollama; });
        QVERIFY(ollama != registry.backends().end());
        QCOMPARE(ollama->name, QStringLiteral("Ollama local"));
        QCOMPARE(ollama->apiKey, QStringLiteral("secret-key"));
        QCOMPARE(ollama->defaultModel, QStringLiteral("llama3.2"));
        QCOMPARE(ollama->baseUrl, QStringLiteral("http://192.168.1.10:11434"));
        QVERIFY(ollama->enabled);
        const auto deepseek =
            std::find_if(registry.backends().begin(), registry.backends().end(),
                         [](const BackendConfig &c) { return c.kind == BackendKind::DeepSeek; });
        QVERIFY(deepseek != registry.backends().end());
        QVERIFY(!deepseek->enabled);
    }
}

void TestBackendRegistry::removePersistsToDatabase() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ChatDatabase database;
    QVERIFY(database.open(dir.filePath(QStringLiteral("ocapi.sqlite"))));
    BackendRegistry registry;
    registry.attachDatabase(&database);
    auto &config = registry.add(QStringLiteral("temp"), BackendKind::Vllm);
    QVERIFY(registry.remove(config.id));
    BackendRegistry reloaded;
    reloaded.load(database);
    QVERIFY(reloaded.backends().isEmpty());
}

QTEST_GUILESS_MAIN(TestBackendRegistry)
#include "tst_backendregistry.moc"
