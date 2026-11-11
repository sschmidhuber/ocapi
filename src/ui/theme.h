#pragma once

#include "core/uisettings.h"

#include <QString>

class QIcon;

namespace okapi {

// Whether the OS is currently in dark mode (used for Theme::Auto).
[[nodiscard]] bool systemPrefersDark();

// Applies the theme to the application palette immediately (no restart
// needed) and returns whether the effective theme is dark. For Theme::Auto
// the system color scheme is used. Also feeds the web chat view's setTheme()
// via the caller.
[[nodiscard]] bool applyTheme(UiSettings::Theme theme);

// Loads a bundled SVG icon, recoloring the fixed dark stroke/fill color to
// the palette text color so the icon stays visible in dark mode.
[[nodiscard]] QIcon themedIcon(const QString &resourcePath);

} // namespace okapi
