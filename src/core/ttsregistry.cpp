#include "core/ttsregistry.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace ocapi {

namespace {

constexpr auto kServicesKey = "tts/services";
constexpr auto kOutputDeviceKey = "tts/outputDevice";
constexpr auto kActiveServiceKey = "tts/activeService";
constexpr auto kSpeakResponsesKey = "tts/speakResponses";

} // namespace

TtsRegistry::TtsRegistry(QObject *parent) : QObject(parent) {}

void TtsRegistry::loadSettings(QSettings &settings) {
    m_services.clear();
    const QJsonDocument document =
        QJsonDocument::fromJson(settings.value(kServicesKey).toString().toUtf8());
    const QJsonArray array = document.array();
    for (const auto &value : array) {
        TtsServiceConfig config = TtsServiceConfig::fromJson(value.toObject());
        if (!config.id.isNull())
            m_services.push_back(config);
    }
    m_outputDevice = settings.value(kOutputDeviceKey).toString();
    m_activeServiceId = settings.value(kActiveServiceKey).toString();
    m_speakResponses = settings.value(kSpeakResponsesKey, false).toBool();
    // Persist through a fresh QSettings created from the path/format: the
    // caller's object is frequently stack-local (MainWindow opens one per
    // load), so keeping a pointer to it dangles and crashes on save().
    m_persistPath = settings.fileName();
    m_persistFormat = settings.format();
    emit changed();
}

void TtsRegistry::saveSettings(QSettings &settings) const {
    QJsonArray array;
    for (const auto &service : m_services)
        array.append(service.toJson());
    settings.setValue(kServicesKey,
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings.setValue(kOutputDeviceKey, m_outputDevice);
    settings.setValue(kActiveServiceKey, m_activeServiceId);
    settings.setValue(kSpeakResponsesKey, m_speakResponses);
}

void TtsRegistry::save() const {
    if (m_persistPath.isEmpty())
        return;
    QSettings settings(m_persistPath, m_persistFormat);
    saveSettings(settings);
}

TtsServiceConfig &TtsRegistry::add(const QString &name, TtsServiceKind kind) {
    TtsServiceConfig config;
    config.name = name;
    config.kind = kind;
    config.baseUrl = TtsServiceConfig::defaultBaseUrl(kind);
    config.model = TtsServiceConfig::defaultModel(kind);
    config.voice = TtsServiceConfig::defaultVoice(kind);
    m_services.push_back(std::move(config));
    if (m_activeServiceId.isEmpty())
        m_activeServiceId = m_services.last().id.toString(QUuid::WithoutBraces);
    save();
    emit changed();
    return m_services.last();
}

bool TtsRegistry::remove(const QUuid &id) {
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

TtsServiceConfig *TtsRegistry::find(const QUuid &id) {
    for (auto &config : m_services) {
        if (config.id == id)
            return &config;
    }
    return nullptr;
}

QVector<TtsServiceConfig> TtsRegistry::enabled() const {
    QVector<TtsServiceConfig> result;
    for (const auto &config : m_services) {
        if (config.enabled)
            result.push_back(config);
    }
    return result;
}

void TtsRegistry::setSpeakResponses(bool enabled) {
    if (m_speakResponses == enabled)
        return;
    m_speakResponses = enabled;
    save();
    emit changed();
}

void TtsRegistry::setOutputDevice(const QString &deviceId) {
    if (m_outputDevice == deviceId)
        return;
    m_outputDevice = deviceId;
    save();
    emit changed();
}

void TtsRegistry::setActiveServiceId(const QString &id) {
    if (m_activeServiceId == id)
        return;
    m_activeServiceId = id;
    save();
    emit changed();
}

} // namespace ocapi
