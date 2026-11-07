#include "core/uisettings.h"

namespace ocapi {

namespace {

constexpr auto kShowThroughputKey = "ui/showThroughput";

} // namespace

UiSettings::UiSettings(QObject *parent) : QObject(parent) {}

void UiSettings::setShowThroughput(bool enabled) {
    if (m_showThroughput == enabled)
        return;
    m_showThroughput = enabled;
    save();
    emit changed();
}

void UiSettings::load(QSettings &settings) {
    m_showThroughput = settings.value(QString::fromLatin1(kShowThroughputKey), false).toBool();
    // Persist through a fresh QSettings created from the path/format: the
    // caller's object is frequently stack-local (MainWindow opens one per
    // load), so keeping a pointer to it would dangle on save().
    m_persistPath = settings.fileName();
    m_persistFormat = settings.format();
    emit changed();
}

void UiSettings::save(QSettings &settings) const {
    settings.setValue(QString::fromLatin1(kShowThroughputKey), m_showThroughput);
}

void UiSettings::save() const {
    if (m_persistPath.isEmpty())
        return;
    QSettings settings(m_persistPath, m_persistFormat);
    save(settings);
}

} // namespace ocapi
