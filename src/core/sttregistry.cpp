#include "core/sttregistry.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace ocapi {

namespace {

constexpr auto kServicesKey = "stt/services";
constexpr auto kInputDeviceKey = "stt/inputDevice";
constexpr auto kActiveServiceKey = "stt/activeService";

} // namespace

SttRegistry::SttRegistry(QObject *parent) : QObject(parent) {}

void SttRegistry::loadSettings(QSettings &settings) {
    m_services.clear();
    const QJsonDocument document =
        QJsonDocument::fromJson(settings.value(kServicesKey).toString().toUtf8());
    const QJsonArray array = document.array();
    for (const auto &value : array) {
        SttServiceConfig config = SttServiceConfig::fromJson(value.toObject());
        if (!config.id.isNull())
            m_services.push_back(config);
    }
    m_inputDevice = settings.value(kInputDeviceKey).toString();
    m_activeServiceId = settings.value(kActiveServiceKey).toString();
    m_persistSettings = &settings;
    emit changed();
}

void SttRegistry::saveSettings(QSettings &settings) const {
    QJsonArray array;
    for (const auto &service : m_services)
        array.append(service.toJson());
    settings.setValue(kServicesKey,
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.setValue(kInputDeviceKey, m_inputDevice);
    settings.setValue(kActiveServiceKey, m_activeServiceId);
}

void SttRegistry::save() const {
    if (m_persistSettings)
        saveSettings(*m_persistSettings);
}

SttServiceConfig &SttRegistry::add(const QString &name, SttServiceKind kind) {
    SttServiceConfig config;
    config.name = name;
    config.kind = kind;
    config.baseUrl = SttServiceConfig::defaultBaseUrl(kind);
    config.model = SttServiceConfig::defaultModel(kind);
    m_services.push_back(std::move(config));
    if (m_activeServiceId.isEmpty())
        m_activeServiceId = m_services.last().id.toString(QUuid::WithoutBraces);
    save();
    emit changed();
    return m_services.last();
}

bool SttRegistry::remove(const QUuid &id) {
    for (auto it = m_services.begin(); it != m_services.end(); ++it) {
        if (it->id == id) {
            m_services.erase(it);
            if (m_activeServiceId == id.toString(QUuid::WithoutBraces)) {
                m_activeServiceId = m_services.isEmpty()
                                        ? QString()
                                        : m_services.first().id.toString(QUuid::WithoutBraces);
            }
            save();
            emit changed();
            return true;
        }
    }
    return false;
}

SttServiceConfig *SttRegistry::find(const QUuid &id) {
    for (auto &config : m_services) {
        if (config.id == id)
            return &config;
    }
    return nullptr;
}

QVector<SttServiceConfig> SttRegistry::enabled() const {
    QVector<SttServiceConfig> result;
    for (const auto &config : m_services) {
        if (config.enabled)
            result.push_back(config);
    }
    return result;
}

void SttRegistry::setInputDevice(const QString &deviceId) {
    if (m_inputDevice == deviceId)
        return;
    m_inputDevice = deviceId;
    save();
    emit changed();
}

void SttRegistry::setActiveServiceId(const QString &id) {
    if (m_activeServiceId == id)
        return;
    m_activeServiceId = id;
    save();
    emit changed();
}

} // namespace ocapi
