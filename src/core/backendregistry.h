#pragma once

#include "core/backendconfig.h"

#include <QObject>
#include <QVector>

namespace ocapi {

class BackendRegistry : public QObject {
    Q_OBJECT

public:
    explicit BackendRegistry(QObject *parent = nullptr);

    void load();
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
    QVector<BackendConfig> m_backends;
};

} // namespace ocapi
