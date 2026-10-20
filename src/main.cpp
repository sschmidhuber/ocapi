#include "ui/mainwindow.h"

#include <QApplication>
#include <QStyleFactory>

namespace {

// Prefer a desktop-native look: Fusion on non-KDE, and breeze when running
// under KDE Plasma where the platform theme already provides it.
void applyDesktopStyle(QApplication &app) {
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP").toLower();
    const bool isKde = desktop.contains("kde");
    if (!isKde && QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ocapi"));
    QApplication::setOrganizationName(QStringLiteral("sschmidhuber"));
    QApplication::setApplicationDisplayName(QStringLiteral("ocapi"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icons/ocapi.svg")));

    applyDesktopStyle(app);

    ocapi::MainWindow window;
    window.show();

    return QApplication::exec();
}
