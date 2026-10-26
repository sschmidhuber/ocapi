#pragma once

#include "core/sttconfig.h"

#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QVector>

namespace ocapi {

// Manages the list of configured speech-to-text services. Analogous to
// BackendRegistry, but persisted as one JSON document in the settings table
// because the service list is small and has no relational queries.
class SttRegistry : public QObject {
    Q_OBJECT

public:
    explicit SttRegistry(QObject *parent = nullptr);

    void loadSettings(QSettings &settings);
    void saveSettings(QSettings &settings) const;
    void save() const;

    [[nodiscard]] const QVector<SttServiceConfig> &services() const { return m_services; }
    SttServiceConfig &add(const QString &name, SttServiceKind kind);
    bool remove(const QUuid &id);
    [[nodiscard]] SttServiceConfig *find(const QUuid &id);
    [[nodiscard]] SttServiceConfig *find(const QString &id) { return find(QUuid(id)); }
    [[nodiscard]] QVector<SttServiceConfig> enabled() const;

    [[nodiscard]] QString inputDevice() const { return m_inputDevice; }
    void setInputDevice(const QString &deviceId);
    [[nodiscard]] QString activeServiceId() const { return m_activeServiceId; }
    void setActiveServiceId(const QString &id);

signals:
    void changed();

private:
    QVector<SttServiceConfig> m_services;
    QString m_inputDevice;     // empty = system default
    QString m_activeServiceId; // empty = first enabled service
    // Where loadSettings() read from; save() reopens the file instead of
    // keeping a pointer to the (often stack-local) caller's QSettings.
    QString m_persistPath;
    QSettings::Format m_persistFormat = QSettings::IniFormat;
};

} // namespace ocapi
