#pragma once

#include <QString>

namespace ocapi {

// Opens files with the platform's default application. QDesktopServices
// routes through the freedesktop OpenURI portal inside Flatpak/snap
// sandboxes and through xdg-open (or the native ShellExecute /
// LaunchServices entry points on Windows/macOS) everywhere else.
class AttachmentOpener {
public:
    // Opens \p path with the system's default application for its type.
    // Returns false (and fills \p error) when the file cannot be opened,
    // e.g. because it no longer exists or no default application is set.
    [[nodiscard]] static bool open(const QString &path, QString *error = nullptr);
};

} // namespace ocapi
