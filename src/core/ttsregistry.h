#pragma once

#include "core/ttsconfig.h"

#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QVector>

namespace ocapi {

// Manages the list of configured text-to-speech services. Analogous to
// SttRegistry, but persisted as one JSON document in the settings table
// because the service list is small and has no relational queries.
class TtsRegistry : public QObject {
    Q_OBJECT

public:
    explicit TtsRegistry(QObject *parent = nullptr);

    void loadSettings(QSettings &settings);
    void saveSettings(QSettings &settings) const;
    void save() const;

    [[nodiscard]] const QVector<TtsServiceConfig> &services() const { return m_services; }
    TtsServiceConfig &add(const QString &name, TtsServiceKind kind);
    bool remove(const QUuid &id);
    [[nodiscard]] TtsServiceConfig *find(const QUuid &id);
    [[nodiscard]] TtsServiceConfig *find(const QString &id) { return find(QUuid(id)); }
    [[nodiscard]] QVector<TtsServiceConfig> enabled() const;

    // Master switch for speaking assistant messages aloud.
    [[nodiscard]] bool speakResponses() const { return m_speakResponses; }
    void setSpeakResponses(bool enabled);

    [[nodiscard]] QString outputDevice() const { return m_outputDevice; }
    void setOutputDevice(const QString &deviceId);
    [[nodiscard]] QString activeServiceId() const { return m_activeServiceId; }
    void setActiveServiceId(const QString &id);

signals:
    void changed();

private:
    QVector<TtsServiceConfig> m_services;
    QString m_outputDevice;    // empty = system default
    QString m_activeServiceId; // empty = first enabled service
    bool m_speakResponses = false;
    QSettings *m_persistSettings = nullptr;
};

} // namespace ocapi
