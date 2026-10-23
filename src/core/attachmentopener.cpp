#include "core/attachmentopener.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

namespace ocapi {

bool AttachmentOpener::open(const QString &path, QString *error) {
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (error)
            *error = QObject::tr("File no longer exists: %1").arg(path);
        return false;
    }
    // QDesktopServices::openUrl() already routes through the OpenURI
    // portal when running inside a Flatpak/snap sandbox, and through
    // xdg-open (or the platform equivalent) everywhere else.
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return true;
    if (error)
        *error = QObject::tr("No default application found for %1").arg(path);
    return false;
}

} // namespace ocapi
