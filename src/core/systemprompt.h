#pragma once

#include <QDateTime>
#include <QString>

class QSettings;

namespace okapi {

// Stores the user-configurable system prompt template and substitutes the
// dynamic fields {{date}}, {{time}}, {{language}} and {{os}} in it.
class SystemPrompt {
public:
    static QString defaultTemplate();
    // Replaces the dynamic fields (case-insensitive): {{date}} and {{time}}
    // from \p now in the system locale, {{language}} from the system locale,
    // {{os}} from QSysInfo (includes version/distribution where applicable).
    static QString substitute(const QString &templ,
                              const QDateTime &now = QDateTime::currentDateTime());

    void load(QSettings &settings);
    void save(QSettings &settings) const;

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }
    // The template with all dynamic fields substituted for right now.
    [[nodiscard]] QString substituted() const { return substitute(m_text); }

private:
    QString m_text;
};

} // namespace okapi
