#include "ui/mainwindow.h"

#include "core/backendconfig.h"
#include "core/backendregistry.h"
#include "core/mcpregistry.h"
#include "core/mcpserverconfig.h"
#include "storage/chatdatabase.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
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
    // Seed two backends on the same mock server, so the model picker has
    // two providers and the "new chat keeps backend+model" E2E test can
    // switch between them.
    QString firstId;
    for (const auto &config : registry.backends()) {
        if (config.baseUrl == url)
            firstId = config.id.toString(QUuid::WithoutBraces);
    }
    if (firstId.isEmpty()) {
        auto &config = registry.add(QStringLiteral("Mock backend"), ocapi::BackendKind::Ollama);
        config.baseUrl = url;
        config.enabled = true;
        firstId = config.id.toString(QUuid::WithoutBraces);
    }
    const QString secondUrl = url + QStringLiteral("/");
    bool secondExists = false;
    for (const auto &config : registry.backends()) {
        if (config.baseUrl == secondUrl)
            secondExists = true;
    }
    if (!secondExists) {
        auto &second = registry.add(QStringLiteral("Mock backend 2"), ocapi::BackendKind::Ollama);
        second.baseUrl = secondUrl;
        second.enabled = true;
    }
    registry.save();

    // The settings page no longer offers a default model; the E2E tests
    // expect the seeded backend to come up with "mock-model" selected, so
    // persist it as the first backend's last model selection.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("models/last/%1").arg(firstId), QStringLiteral("mock-model"));

    // With OCAPI_E2E_MOCK_MCP=1, also seed an MCP server pointing at the
    // mock server from tests/e2e/mock_mcp.py. Its tools are allowed without
    // a confirmation dialog so the chat flow can run unattended.
    if (!qEnvironmentVariableIsSet("OCAPI_E2E_MOCK_MCP"))
        return;
    const QByteArray mcpPort = qgetenv("OCAPI_E2E_MOCK_MCP_PORT");
    const QString mcpUrl =
        QStringLiteral("http://127.0.0.1:") +
        (mcpPort.isEmpty() ? QStringLiteral("11435") : QString::fromLatin1(mcpPort));
    ocapi::McpRegistry mcpRegistry;
    mcpRegistry.load(database);
    for (const auto &server : mcpRegistry.servers()) {
        if (server.url == mcpUrl)
            return;
    }
    ocapi::McpServerConfig &server =
        mcpRegistry.add(QStringLiteral("Mock MCP"), ocapi::McpTransport::StreamableHttp);
    server.url = mcpUrl;
    server.defaultPermission = ocapi::McpPermission::Allow;
    server.enabled = true;
    mcpRegistry.save();
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
    // The display name is what Qt appends to the window title when the title
    // does not already end with it (QPlatformWindow); keep it in sync with
    // the title "Ocapi" so the title bar shows just "Ocapi", not
    // "Ocapi -- ocapi". The application name stays lowercase: it determines
    // the config/data paths (ocapi.conf, ocapi.sqlite).
    QApplication::setApplicationDisplayName(QStringLiteral("Ocapi"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icons/ocapi.png")));

    applyDesktopStyle(app);

    if (qEnvironmentVariableIsSet("OCAPI_E2E_MOCK_BACKEND"))
        seedE2eMockBackend();

    ocapi::MainWindow window;
    window.show();

    return QApplication::exec();
}
