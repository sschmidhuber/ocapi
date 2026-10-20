#pragma once

#include "core/backendconfig.h"
#include <QObject>
#include <QVector>

namespace ocapi {

class ChatDatabase;

class BackendRegistry : public QObject {
    Q_OBJECT
public:
    explicit BackendRegistry(QObject *parent = nullptr);

    void loadFromSettings();
    void saveToSettings() const;
    void load(ChatDatabase &database);
    void attachDatabase(ChatDatabase *database);
    void save() const;
    [[nodiscard]] const QVector<BackendConfig> &backends() const { return m_backends; }
    BackendConfig &add(const QString &name, BackendKind kind);
    bool remove(const QUuid &id);
    BackendConfig *find(const QUuid &id);
    [[nodiscard]] BackendConfig *find(const QString &id) { return find(QUuid(id)); }
    [[nodiscard]] QVector<BackendConfig> enabled() const;

signals:
    void changed();

private:
    void persistToDatabase(const BackendConfig &config) const;

    QVector<BackendConfig> m_backends;
    ChatDatabase *m_database = nullptr;
};

} // namespace ocapi
