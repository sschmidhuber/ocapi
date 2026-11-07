#pragma once

#include <QObject>
#include <QSettings>

namespace ocapi {

// General "User Interface" settings (Settings > General > User Interface),
// persisted in the application settings under the "ui" group. Mirrors the
// TtsRegistry pattern: load() remembers the file so set*() can save()
// immediately, and every change emits changed().
class UiSettings : public QObject {
    Q_OBJECT

public:
    explicit UiSettings(QObject *parent = nullptr);

    // Whether assistant messages show the output token throughput (t/s)
    // next to the copy button.
    [[nodiscard]] bool showThroughput() const { return m_showThroughput; }
    void setShowThroughput(bool enabled);

    void load(QSettings &settings);
    void save(QSettings &settings) const;
    void save() const;

signals:
    void changed();

private:
    bool m_showThroughput = false;
    QString m_persistPath;
    QSettings::Format m_persistFormat = QSettings::IniFormat;
};

} // namespace ocapi
