#include "core/systemprompt.h"

#include <QLocale>
#include <QSettings>
#include <QSysInfo>

namespace okapi {

namespace {
constexpr auto kSettingsKey = "systemPrompt/text";
}

QString SystemPrompt::defaultTemplate() {
    return QStringLiteral("You are a helpful assistant.\n"
                          "Current date: {{date}}, current time: {{time}}.\n"
                          "The user's system language is {{language}}.\n"
                          "The operating system is {{os}}.");
}

QString SystemPrompt::substitute(const QString &templ, const QDateTime &now) {
    const QLocale locale = QLocale::system();
    QString result = templ;
    result.replace(QStringLiteral("{{date}}"), locale.toString(now.date(), QLocale::ShortFormat),
                   Qt::CaseInsensitive);
    result.replace(QStringLiteral("{{time}}"), locale.toString(now.time(), QLocale::ShortFormat),
                   Qt::CaseInsensitive);
    result.replace(QStringLiteral("{{language}}"), locale.name(), Qt::CaseInsensitive);
    result.replace(QStringLiteral("{{os}}"), QSysInfo::prettyProductName(), Qt::CaseInsensitive);
    return result;
}

void SystemPrompt::load(QSettings &settings) {
    m_text = settings.contains(QLatin1String(kSettingsKey))
                 ? settings.value(QLatin1String(kSettingsKey)).toString()
                 : defaultTemplate();
}

void SystemPrompt::save(QSettings &settings) const {
    settings.setValue(QLatin1String(kSettingsKey), m_text);
}

} // namespace okapi
