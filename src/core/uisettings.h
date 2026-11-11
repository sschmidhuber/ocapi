#pragma once

#include <QObject>
#include <QSettings>

namespace okapi {

// General "User Interface" settings (Settings > General > User Interface),
// persisted in the application settings under the "ui" group. Mirrors the
// TtsRegistry pattern: load() remembers the file so set*() can save()
// immediately, and every change emits changed().
class UiSettings : public QObject {
    Q_OBJECT

public:
    // Color theme of the application UI.
    enum class Theme {
        Auto, // follow the system color scheme (default)
        Light,
        Dark,
    };

    explicit UiSettings(QObject *parent = nullptr);

    // Whether assistant messages show the output token throughput (t/s)
    // next to the copy button.
    [[nodiscard]] bool showThroughput() const { return m_showThroughput; }
    void setShowThroughput(bool enabled);

    // Height of the composer's text input in px (the composer area is
    // resizable with the mouse; the value is persisted here). Attachment
    // thumbnails add their own height on top of this base height.
    static constexpr int kMinComposerHeight = 48;
    static constexpr int kMaxComposerHeight = 480;
    [[nodiscard]] int composerHeight() const { return m_composerHeight; }
    void setComposerHeight(int height);

    // Color theme (auto = follow the system); applied immediately on change.
    [[nodiscard]] Theme theme() const { return m_theme; }
    void setTheme(Theme theme);
    [[nodiscard]] static QString themeKey(Theme theme);
    [[nodiscard]] static Theme themeFromString(const QString &key);

    void load(QSettings &settings);
    void save(QSettings &settings) const;
    void save() const;

signals:
    void changed();

private:
    bool m_showThroughput = false;
    int m_composerHeight = 96;
    Theme m_theme = Theme::Auto;
    QString m_persistPath;
    QSettings::Format m_persistFormat = QSettings::IniFormat;
};

} // namespace okapi
