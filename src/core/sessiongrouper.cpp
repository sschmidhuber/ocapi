#include "core/sessiongrouper.h"

#include <QLocale>

namespace ocapi {

QString SessionGrouper::groupKey(const QDateTime &timestamp, const QDateTime &reference) {
    if (!timestamp.isValid())
        return {};
    const QDate date = timestamp.date();
    const QDate today = reference.isValid() ? reference.date() : QDate::currentDate();
    const int daysAgo = static_cast<int>(date.daysTo(today));
    if (daysAgo == 0)
        return QStringLiteral("today");
    if (daysAgo == 1)
        return QStringLiteral("yesterday");
    if (daysAgo >= 2 && daysAgo <= 5)
        // Weekday groups key on the actual date so different weeks do not merge.
        return QStringLiteral("day:%1").arg(date.toString(Qt::ISODate));
    if (date.year() == today.year() && date.month() == today.month())
        return QStringLiteral("monthday:%1").arg(date.toString(Qt::ISODate));
    if (date.year() == today.year())
        return QStringLiteral("month:%1-%2")
            .arg(date.year())
            .arg(date.month(), 2, 10, QLatin1Char('0'));
    return QStringLiteral("year:%1").arg(date.year());
}

QString SessionGrouper::groupLabel(const QString &key) {
    if (key == QLatin1String("today"))
        return QObject::tr("Today");
    if (key == QLatin1String("yesterday"))
        return QObject::tr("Yesterday");
    const QLocale locale;
    if (key.startsWith(QLatin1String("day:"))) {
        const QDate date = QDate::fromString(key.mid(4), Qt::ISODate);
        if (date.isValid())
            return locale.dayName(date.dayOfWeek(), QLocale::LongFormat);
    }
    if (key.startsWith(QLatin1String("monthday:"))) {
        const QDate date = QDate::fromString(key.mid(9), Qt::ISODate);
        if (date.isValid())
            return QStringLiteral("%1 %2")
                .arg(date.day())
                .arg(locale.monthName(date.month(), QLocale::ShortFormat));
    }
    if (key.startsWith(QLatin1String("month:"))) {
        const QStringList parts = key.mid(6).split(QLatin1Char('-'));
        if (parts.size() == 2) {
            const int month = parts.at(1).toInt();
            if (month >= 1 && month <= 12)
                return locale.monthName(month, QLocale::LongFormat);
        }
    }
    if (key.startsWith(QLatin1String("year:")))
        return key.mid(5);
    return key;
}

} // namespace ocapi
