#pragma once

#include <QDateTime>
#include <QString>

namespace ocapi {

// Groups chat sessions into a time-based tree for the sidebar. The group
// label for a timestamp follows the common chat-app convention:
//   - Today
//   - Yesterday
//   - weekday name (e.g. Tuesday) for the last 5 days
//   - day + month name (e.g. "5 Oct") for the current month
//   - month name (e.g. "September") for the current year
//   - year (e.g. "2024") for older entries
// Group keys sort chronologically descending (newest group first), matching
// the sidebar order.
class SessionGrouper {
public:
    // Stable key identifying the group a timestamp belongs to. Groups are
    // ordered by key descending (ISO-like keys sort chronologically).
    [[nodiscard]] static QString
    groupKey(const QDateTime &timestamp, const QDateTime &reference = QDateTime::currentDateTime());

    // Localized, user-facing label for a group key. Returns an empty string
    // for empty/invalid keys.
    [[nodiscard]] static QString groupLabel(const QString &key);
};

} // namespace ocapi
