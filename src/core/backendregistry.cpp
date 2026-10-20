#include "core/backendregistry.h"

#include <QSettings>

namespace ocapi {

namespace {

constexpr auto kGroup = "backends";
constexpr auto kCount = "count";

BackendKind kindFromString(const QString &value) {
    static const QHash<QString, BackendKind> map{
        {QStringLiteral("ollama"), BackendKind::Ollama},
        {QStringLiteral("llamacpp"), BackendKind::LlamaCpp},
        {QStringLiteral("vllm"), BackendKind::Vllm},
        {QStringLiteral("openai"), BackendKind::OpenAi},
        {QStringLiteral("anthropic"), BackendKind::Anthropic},
        {QStringLiteral("gemini"), BackendKind::Gemini},
        {QStringLiteral("mistral"), BackendKind::Mistral},
    };
    return map.value(value, BackendKind::Ollama);
}

QString kindToString(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
        return QStringLiteral("ollama");
    case BackendKind::LlamaCpp:
        return QStringLiteral("llamacpp");
    case BackendKind::Vllm:
        return QStringLiteral("vllm");
    case BackendKind::OpenAi:
        return QStringLiteral("openai");
    case BackendKind::Anthropic:
        return QStringLiteral("anthropic");
    case BackendKind::Gemini:
        return QStringLiteral("gemini");
    case BackendKind::Mistral:
        return QStringLiteral("mistral");
    }
    return {};
}

} // namespace

BackendRegistry::BackendRegistry(QObject *parent) : QObject(parent) {}

void BackendRegistry::load() {
    m_backends.clear();
    QSettings settings;
    settings.beginGroup(kGroup);
    const int count = settings.beginReadArray(kCount);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        BackendConfig config;
        config.id = QUuid(settings.value(QStringLiteral("id")).toString());
        if (config.id.isNull())
            config.id = QUuid::createUuid();
        config.name = settings.value(QStringLiteral("name")).toString();
        config.kind = kindFromString(settings.value(QStringLiteral("kind")).toString());
        config.baseUrl = settings.value(QStringLiteral("baseUrl")).toString();
        if (config.baseUrl.isEmpty())
            config.baseUrl = BackendConfig::defaultBaseUrl(config.kind);
        config.apiKey = settings.value(QStringLiteral("apiKey")).toString();
        config.defaultModel = settings.value(QStringLiteral("defaultModel")).toString();
        config.enabled = settings.value(QStringLiteral("enabled"), true).toBool();
        m_backends.push_back(std::move(config));
    }
    settings.endArray();
    settings.endGroup();
}

void BackendRegistry::save() const {
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.beginWriteArray(kCount);
    for (int i = 0; i < m_backends.size(); ++i) {
        const auto &config = m_backends.at(i);
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("id"), config.id.toString());
        settings.setValue(QStringLiteral("name"), config.name);
        settings.setValue(QStringLiteral("kind"), kindToString(config.kind));
        settings.setValue(QStringLiteral("baseUrl"), config.baseUrl);
        settings.setValue(QStringLiteral("apiKey"), config.apiKey);
        settings.setValue(QStringLiteral("defaultModel"), config.defaultModel);
        settings.setValue(QStringLiteral("enabled"), config.enabled);
    }
    settings.endArray();
    settings.endGroup();
}

BackendConfig &BackendRegistry::add(const QString &name, BackendKind kind) {
    BackendConfig config;
    config.name = name;
    config.kind = kind;
    config.baseUrl = BackendConfig::defaultBaseUrl(kind);
    m_backends.push_back(std::move(config));
    emit changed();
    return m_backends.last();
}

bool BackendRegistry::remove(const QUuid &id) {
    for (auto it = m_backends.begin(); it != m_backends.end(); ++it) {
        if (it->id == id) {
            m_backends.erase(it);
            emit changed();
            return true;
        }
    }
    return false;
}

BackendConfig *BackendRegistry::find(const QUuid &id) {
    for (auto &config : m_backends) {
        if (config.id == id)
            return &config;
    }
    return nullptr;
}

QVector<BackendConfig> BackendRegistry::enabled() const {
    QVector<BackendConfig> result;
    for (const auto &config : m_backends) {
        if (config.enabled)
            result.push_back(config);
    }
    return result;
}

} // namespace ocapi
