#include "core/backendregistry.h"

#include "storage/chatdatabase.h"

namespace ocapi {

BackendRegistry::BackendRegistry(QObject *parent) : QObject(parent) {}

void BackendRegistry::load(ChatDatabase &database) {
    m_backends = database.loadBackends();
    attachDatabase(&database);
}

void BackendRegistry::attachDatabase(ChatDatabase *database) {
    m_databasePath = database ? database->filePath() : QString();
}

void BackendRegistry::save() const {
    if (m_databasePath.isEmpty())
        return;
    ChatDatabase database;
    if (!database.open(m_databasePath))
        return;
    for (const auto &config : m_backends)
        database.insertBackend(config);
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
            deleteFromDatabase(id);
            m_backends.erase(it);
            emit changed();
            return true;
        }
    }
    return false;
}

void BackendRegistry::persistToDatabase(const BackendConfig &config) const {
    if (m_databasePath.isEmpty())
        return;
    ChatDatabase database;
    if (!database.open(m_databasePath))
        return;
    database.insertBackend(config);
}

void BackendRegistry::deleteFromDatabase(const QUuid &id) const {
    if (m_databasePath.isEmpty())
        return;
    ChatDatabase database;
    if (!database.open(m_databasePath))
        return;
    database.deleteBackend(id.toString(QUuid::WithoutBraces));
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
