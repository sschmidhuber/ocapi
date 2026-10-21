#include "ui/mainwindow.h"

#include "core/backendconfig.h"
#include "core/backendregistry.h"
#include "storage/chatdatabase.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QUrl>

namespace {

// When running the end-to-end tests (OCAPI_E2E_MOCK_BACKEND=1), pre-seed a
// backend pointing at the mock server from tests/e2e/mock_ollama.py so the
// tests do not depend on menu-driven backend configuration.
void seedE2eMockBackend() {
    const QByteArray port = qgetenv("OCAPI_E2E_MOCK_PORT");
    const QString url = QStringLiteral("http://127.0.0.1:") +
                        (port.isEmpty() ? QStringLiteral("11434") : QString::fromLatin1(port));

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataDir))
        return;
    ocapi::ChatDatabase database;
    if (!database.open(dataDir + QStringLiteral("/ocapi.sqlite")))
        return;
    ocapi::BackendRegistry registry;
    registry.load(database);
    for (const auto &config : registry.backends()) {
        if (config.baseUrl == url)
            return;
    }
    ocapi::BackendConfig &config =
        registry.add(QStringLiteral("Mock backend"), ocapi::BackendKind::Ollama);
    config.baseUrl = url;
    config.defaultModel = QStringLiteral("mock-model");
    config.enabled = true;
    registry.save();
}

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

    if (qEnvironmentVariableIsSet("OCAPI_E2E_MOCK_BACKEND"))
        seedE2eMockBackend();

    ocapi::MainWindow window;
    window.show();

    return QApplication::exec();
}
