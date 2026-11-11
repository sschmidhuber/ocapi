#include "core/uisettings.h"

#include <algorithm>

namespace okapi {

namespace {

constexpr auto kShowThroughputKey = "ui/showThroughput";
constexpr auto kComposerHeightKey = "ui/composerHeight";
constexpr auto kThemeKey = "ui/theme";

} // namespace

UiSettings::UiSettings(QObject *parent) : QObject(parent) {}

void UiSettings::setShowThroughput(bool enabled) {
    if (m_showThroughput == enabled)
        return;
    m_showThroughput = enabled;
    save();
    emit changed();
}

void UiSettings::setComposerHeight(int height) {
    const int clamped = std::clamp(height, kMinComposerHeight, kMaxComposerHeight);
    if (m_composerHeight == clamped)
        return;
    m_composerHeight = clamped;
    save();
    emit changed();
}

QString UiSettings::themeKey(UiSettings::Theme theme) {
    switch (theme) {
    case UiSettings::Theme::Auto:
        return QStringLiteral("auto");
    case UiSettings::Theme::Light:
        return QStringLiteral("light");
    case UiSettings::Theme::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("auto");
}

UiSettings::Theme UiSettings::themeFromString(const QString &key) {
    if (key == QLatin1String("light"))
        return UiSettings::Theme::Light;
    if (key == QLatin1String("dark"))
        return UiSettings::Theme::Dark;
    return UiSettings::Theme::Auto;
}

void UiSettings::setTheme(UiSettings::Theme theme) {
    if (m_theme == theme)
        return;
    m_theme = theme;
    save();
    emit changed();
}

void UiSettings::load(QSettings &settings) {
    m_showThroughput = settings.value(QString::fromLatin1(kShowThroughputKey), false).toBool();
    m_composerHeight = std::clamp(
        settings.value(QString::fromLatin1(kComposerHeightKey), m_composerHeight).toInt(),
        kMinComposerHeight, kMaxComposerHeight);
    m_theme = themeFromString(
        settings.value(QString::fromLatin1(kThemeKey), themeKey(m_theme)).toString());
    // Persist through a fresh QSettings created from the path/format: the
    // caller's object is frequently stack-local (MainWindow opens one per
    // load), so keeping a pointer to it would dangle on save().
    m_persistPath = settings.fileName();
    m_persistFormat = settings.format();
    emit changed();
}

void UiSettings::save(QSettings &settings) const {
    settings.setValue(QString::fromLatin1(kShowThroughputKey), m_showThroughput);
    settings.setValue(QString::fromLatin1(kComposerHeightKey), m_composerHeight);
    settings.setValue(QString::fromLatin1(kThemeKey), themeKey(m_theme));
}

void UiSettings::save() const {
    if (m_persistPath.isEmpty())
        return;
    QSettings settings(m_persistPath, m_persistFormat);
    save(settings);
}

} // namespace okapi
