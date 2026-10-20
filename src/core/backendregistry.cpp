#include "core/backendregistry.h"

#include "storage/chatdatabase.h"

#include <QSettings>

namespace ocapi {

namespace {

constexpr auto kGroup = "backends";
constexpr auto kCount = "count";

} // namespace

BackendRegistry::BackendRegistry(QObject *parent) : QObject(parent) {}

void BackendRegistry::loadFromSettings() {
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
        config.kind =
            BackendConfig::kindFromString(settings.value(QStringLiteral("kind")).toString());
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

void BackendRegistry::saveToSettings() const {
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.beginWriteArray(kCount);
    for (int i = 0; i < m_backends.size(); ++i) {
        const auto &config = m_backends.at(i);
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("id"), config.id.toString());
        settings.setValue(QStringLiteral("name"), config.name);
        settings.setValue(QStringLiteral("kind"), BackendConfig::kindKey(config.kind));
        settings.setValue(QStringLiteral("baseUrl"), config.baseUrl);
        settings.setValue(QStringLiteral("apiKey"), config.apiKey);
        settings.setValue(QStringLiteral("defaultModel"), config.defaultModel);
        settings.setValue(QStringLiteral("enabled"), config.enabled);
    }
    settings.endArray();
    settings.endGroup();
}

void BackendRegistry::load(ChatDatabase &database) {
    m_backends = database.loadBackends();
    attachDatabase(&database);
}

void BackendRegistry::attachDatabase(ChatDatabase *database) {
    m_database = database;
}

void BackendRegistry::save() const {
    if (m_database) {
        for (const auto &config : m_backends)
            m_database->insertBackend(config);
        return;
    }
    saveToSettings();
}

BackendConfig &BackendRegistry::add(const QString &name, BackendKind kind) {
    BackendConfig config;
    config.name = name;
    config.kind = kind;
    config.baseUrl = BackendConfig::defaultBaseUrl(kind);
    m_backends.push_back(std::move(config));
    persistToDatabase(m_backends.last());
    emit changed();
    return m_backends.last();
}

bool BackendRegistry::remove(const QUuid &id) {
    for (auto it = m_backends.begin(); it != m_backends.end(); ++it) {
        if (it->id == id) {
            if (m_database)
                m_database->deleteBackend(id.toString(QUuid::WithoutBraces));
            m_backends.erase(it);
            emit changed();
            return true;
        }
    }
    return false;
}

void BackendRegistry::persistToDatabase(const BackendConfig &config) const {
    if (m_database)
        m_database->insertBackend(config);
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
