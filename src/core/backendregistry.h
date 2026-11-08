#pragma once

#include "core/backendconfig.h"
#include <QObject>
#include <QString>
#include <QVector>

namespace ocapi {

class ChatDatabase;

// Persists through a short-lived ChatDatabase reopened from the stored file
// path (same idiom as SttRegistry/TtsRegistry reopening QSettings by path):
// retaining a ChatDatabase* would dangle if this registry outlives the
// caller's database object (#68).
class BackendRegistry : public QObject {
    Q_OBJECT
public:
    explicit BackendRegistry(QObject *parent = nullptr);

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
    void deleteFromDatabase(const QUuid &id) const;

    QVector<BackendConfig> m_backends;
    QString m_databasePath;
};

} // namespace ocapi
