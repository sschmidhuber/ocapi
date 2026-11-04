#include "ui/mainwindow.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/chatsession.h"
#include "core/tooldebug.h"
#include "storage/chatdatabase.h"
#include "ui/composer.h"
#include "ui/settingsdialog.h"
#include "ui/sidebar.h"
#include "ui/webchatview.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ocapi {

namespace {

constexpr int kMessagesPerPage = 50;

} // namespace

ModelPickerButton::ModelPickerButton(QWidget *parent) : QToolButton(parent) {
    setPopupMode(QToolButton::InstantPopup);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    // No explicit accessible name: AT-SPI then exposes the button text
    // ("Backend | model"), which the E2E test relies on.
    connect(this, &QToolButton::clicked, this, &ModelPickerButton::showMenu);
}

void ModelPickerButton::setBackendAndModel(const QString &backendName, const QString &backendId,
                                           const QString &model) {
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
        qWarning().noquote() << "[ocapi models] setBackendAndModel" << backendName << backendId
                             << "model=" << model
                             << "listForBackend=" << m_models.value(backendId).size();
    const bool changed = m_backendId != backendId || m_model != model;
    m_backendName = backendName;
    m_backendId = backendId;
    m_model = model;
    const QString label = model.isEmpty() ? backendName : tr("%1 | %2").arg(backendName, model);
    setText(label.isEmpty() ? tr("Select backend and model…") : label);
    if (model.isEmpty() || m_models.value(m_backendId).contains(model)) {
        if (changed)
            rebuildMenu();
        return;
    }
    QStringList entry = m_models.value(m_backendId);
    entry.append(model);
    m_models.insert(m_backendId, entry);
    if (changed)
        rebuildMenu();
}

void ModelPickerButton::setBackends(const QVector<QPair<QString, QString>> &backends,
                                    const QHash<QString, QStringList> &models) {
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS")) {
        QStringList perBackend;
        for (auto it = models.begin(); it != models.end(); ++it)
            perBackend << it.key() + QLatin1Char(':') + QString::number(it.value().size());
        qWarning().noquote() << "[ocapi models] setBackends backends=" << backends.size()
                             << "modelLists=" << perBackend.join(QLatin1Char(' '));
    }
    m_backends = backends;
    // Sort providers alphabetically by display name, and each provider's
    // models alphabetically, so the menu does not depend on the order in
    // which backends were configured or models detected.
    std::ranges::sort(m_backends, [](const auto &a, const auto &b) {
        return a.first.localeAwareCompare(b.first) < 0;
    });
    m_models = models;
    for (auto &list : m_models)
        std::ranges::sort(
            list, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    rebuildMenu();
}

void ModelPickerButton::addModels(const QString &backendId, const QStringList &models) {
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
        qWarning().noquote() << "[ocapi models] addModels called for" << backendId
                             << "new=" << models.size()
                             << "currentListSize=" << m_models.value(backendId).size()
                             << "backendsInPicker=" << m_backends.size();
    QStringList list = m_models.value(backendId);
    for (const QString &model : models) {
        if (!list.contains(model))
            list.append(model);
    }
    std::ranges::sort(
        list, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    m_models.insert(backendId, list);
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
        qWarning().noquote() << "[ocapi models] addModels" << backendId << "->" << list.size()
                             << list;
    rebuildMenu();
}

void ModelPickerButton::rebuildMenu() {
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS")) {
        QStringList perBackend;
        for (const auto &[name, id] : m_backends)
            perBackend << name + QLatin1Char('(') + QString::number(m_models.value(id).size()) +
                              QLatin1Char(')');
        qWarning().noquote() << "[ocapi models] rebuildMenu current=" << m_backendName << '|'
                             << m_model << "entries=" << perBackend.join(QLatin1Char(' '));
    }
    auto *menu = new QMenu(this);
    for (const auto &[name, id] : m_backends) {
        auto *backendAction = menu->addAction(name);
        backendAction->setCheckable(true);
        backendAction->setChecked(id == m_backendId);
        connect(backendAction, &QAction::triggered, this, [this, id, name]() {
            const QStringList sorted = m_models.value(id);
            setBackendAndModel(name, id, sorted.value(0));
            emit backendSelected(id);
        });
        auto *section = new QMenu(name, menu);
        backendAction->setMenu(section);
        const QStringList list = m_models.value(id);
        if (list.isEmpty()) {
            auto *none = section->addAction(tr("No models detected yet"));
            none->setEnabled(false);
        }
        for (const QString &model : list) {
            auto *action = section->addAction(model);
            action->setCheckable(true);
            action->setChecked(id == m_backendId && model == m_model);
            connect(action, &QAction::triggered, this, [this, id, name, model]() {
                setBackendAndModel(name, id, model);
                emit modelSelected(id, model);
            });
        }
    }
    setMenu(menu);
}

namespace {

QString defaultDatabasePath() {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataDir))
        return QStringLiteral("ocapi.sqlite");
    return dataDir + QStringLiteral("/ocapi.sqlite");
}

Role roleFromName(const QString &name) {
    if (name == QLatin1String("system"))
        return Role::System;
    if (name == QLatin1String("assistant"))
        return Role::Assistant;
    if (name == QLatin1String("tool"))
        return Role::Tool;
    return Role::User;
}

Message storedToMessage(const StoredMessage &stored) {
    Message message;
    message.id = QUuid(stored.id);
    message.role = roleFromName(stored.role);
    message.content = stored.content;
    message.reasoningSegments = stored.reasoningSegments;
    message.elementOrder = stored.elementOrder;
    message.timestamp = stored.timestamp;
    message.streaming = false;
    message.error = stored.error;
    message.tokensPerSecond = stored.tokensPerSecond;
    message.images = stored.images;
    message.files = stored.files;
    message.toolCalls = stored.toolCalls;
    return message;
}

void appendStoredMessage(ChatSession *session, const StoredMessage &stored) {
    const Message message = storedToMessage(stored);
    if (!message.id.isNull())
        session->appendMessage(message);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_mcpManager(&m_mcpRegistry) {
    setWindowTitle(tr("ocapi"));
    resize(1100, 720);

    if (!openDatabase())
        statusBar()->showMessage(tr("Warning: chat storage unavailable, chats will not be "
                                    "persisted."));
    loadBackendsFromDatabase();
    loadMcpServersFromDatabase();
    loadSttSettingsFromDatabase();
    loadLastModels();

    setupUi();
    setupConnections();

    // Voice input setup needs the composer, which setupUi() creates.
    configureVoiceInput();

    // TTS setup needs m_speakButton, which setupUi() creates.
    loadTtsSettingsFromDatabase();
    configureTts();

    m_mcpManager.reload();
    onMcpToolsChanged();

    onBackendChanged();
    refreshSidebar();

    if (m_sessions.isEmpty())
        createSession();
    else
        onSessionSelected(m_sessions.first()->id());
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    // The chat composer is where a conversation starts: focus it on every
    // show of the main window (first launch and re-show alike) so the user
    // can start typing immediately.
    if (m_composer)
        m_composer->focusInput();
}

void MainWindow::setupUi() {
    // Plasma-style toolbar across the top: flat buttons with icon and text.
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_newChatButton = new QToolButton(m_toolBar);
    m_newChatButton->setText(tr("New chat"));
    m_newChatButton->setIcon(
        QIcon(QString::fromUtf8(":/resources/icons/contact-new-symbolic.svg")));
    // Widgets added via addWidget() don't inherit the toolbar's
    // toolButtonStyle; set it explicitly so icon and text both show.
    m_newChatButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_newChatButton->setToolTip(tr("Start a new chat"));
    m_settingsButton = new QToolButton(m_toolBar);
    m_settingsButton->setText(tr("Settings"));
    m_settingsButton->setIcon(
        QIcon(QString::fromUtf8(":/resources/icons/settings-configure-symbolic.svg")));
    m_settingsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsButton->setToolTip(tr("Open settings"));
    m_toolBar->addWidget(m_newChatButton);
    m_toolBar->addWidget(m_settingsButton);

    auto *central = new QWidget(this);
    auto *centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto *splitter = new QSplitter(central);

    m_sidebar = new Sidebar(splitter);
    m_sidebar->setMinimumWidth(200);
    m_sidebar->setMaximumWidth(320);

    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Constant margin around the chat conversation area, matching the
    // sidebar's 8px gap. A slim scrollbar keeps the chat box the same
    // width whether the scrollbar is shown or not.
    constexpr int kPanelMargin = 8;
    m_chatView = new WebChatView(rightPanel);
    auto *chatMargin = new QVBoxLayout();
    chatMargin->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin, 0);
    chatMargin->setSpacing(kPanelMargin);
    chatMargin->addWidget(m_chatView);
    auto *chatMarginHost = new QWidget(rightPanel);
    chatMarginHost->setLayout(chatMargin);
    rightLayout->addWidget(chatMarginHost, 1);

    m_emptyLabel =
        new QLabel(tr("Select a chat or start a new one.\n\n"
                      "Set up a backend via \"Settings… → Model backends\" to get started."),
                   rightPanel);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightLayout->addWidget(m_emptyLabel);
    m_emptyLabel->hide();

    auto *composerColumn = new QWidget(rightPanel);
    auto *composerColumnLayout = new QVBoxLayout(composerColumn);
    composerColumnLayout->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin,
                                             kPanelMargin);
    composerColumnLayout->setSpacing(4);
    m_composer = new Composer(composerColumn);
    m_composer->setMaximumHeight(160);
    composerColumnLayout->addWidget(m_composer);

    // Row below the input: model picker, audio and file input buttons,
    // then send/stop.
    auto *footerRow = new QWidget(composerColumn);
    auto *footerLayout = new QHBoxLayout(footerRow);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);
    m_modelPicker = new ModelPickerButton(footerRow);
    m_modelPicker->setToolTip(tr("Select model backend and model"));
    m_sendButton = new QPushButton(tr("Send"), footerRow);
    m_stopButton = new QPushButton(tr("Stop"), footerRow);
    m_stopButton->setVisible(false);
    // Icon button styled like the Dictate (mic) button next to it.
    m_speakButton = new QToolButton(footerRow);
    const QIcon speakerIcon(QString::fromUtf8(":/resources/icons/audio-symbolic.svg"));
    if (!speakerIcon.isNull()) {
        m_speakButton->setIcon(speakerIcon);
        m_speakButton->setIconSize(QSize(18, 18));
    } else {
        m_speakButton->setText(QString::fromUtf8("\U0001F50A"));
    }
    m_speakButton->setCheckable(true);
    m_speakButton->setAutoRaise(true);
    m_speakButton->setFixedSize(28, 28);
    m_speakButton->setToolTip(tr("Audio output (text-to-speech)"));
    m_speakButton->setAccessibleName(tr("Speak assistant responses aloud"));
    m_speakButton->setVisible(false);
    footerLayout->addWidget(m_modelPicker, 1);
    footerLayout->addStretch(1);
    m_composer->micButton()->setParent(footerRow);
    m_composer->attachButton()->setParent(footerRow);
    footerLayout->addWidget(m_composer->micButton());
    footerLayout->addWidget(m_composer->attachButton());
    footerLayout->addWidget(m_speakButton);
    footerLayout->addWidget(m_sendButton);
    footerLayout->addWidget(m_stopButton);
    composerColumnLayout->addWidget(footerRow);
    rightLayout->addWidget(composerColumn);

    splitter->addWidget(m_sidebar);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(1, 1);
    centralLayout->addWidget(splitter);
    setCentralWidget(central);

    m_statusLabel = new QLabel(statusBar());
    statusBar()->addPermanentWidget(m_statusLabel);
    m_mcpStatusLabel = new QLabel(statusBar());
    statusBar()->addPermanentWidget(m_mcpStatusLabel);
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::setupConnections() {
    connect(m_newChatButton, &QToolButton::clicked, this, &MainWindow::onNewChat);
    connect(m_settingsButton, &QToolButton::clicked, this, [this] { openSettings(QString()); });
    connect(m_sidebar, &Sidebar::sessionSelected, this, &MainWindow::onSessionSelected);
    connect(m_sidebar, &Sidebar::sessionDeleteRequested, this,
            &MainWindow::onSessionDeleteRequested);
    connect(m_composer, &Composer::sendRequested, this,
            [this](const QString &text, const QVector<ImageAttachment> &images,
                   const QVector<FileAttachment> &files) { onSend(text, images, files); });
    connect(m_composer, &Composer::dictationRequested, this, [this]() {
        if (m_voiceInput.isActive())
            m_voiceInput.stop();
        else if (!m_voiceInput.start())
            m_composer->setDictationActive(false);
    });
    connect(&m_voiceInput, &VoiceInput::started, this,
            [this]() { m_composer->setDictationActive(true); });
    connect(&m_voiceInput, &VoiceInput::stopped, this,
            [this]() { m_composer->setDictationActive(false); });
    // Live transcription: while dictating, already-spoken words replace the
    // dictated prefix of the input field, so the user sees text as they speak.
    connect(&m_voiceInput, &VoiceInput::intermediateTranscription, this,
            [this](const QString &text) {
                m_dictatedText = text;
                m_composer->setDictatedText(text);
            });
    connect(&m_voiceInput, &VoiceInput::transcriptionUpdated, this, [this](const QString &text) {
        m_dictatedText.clear();
        m_composer->finishDictation(text);
        statusBar()->showMessage(tr("Dictation finished."), 5000);
    });
    connect(&m_voiceInput, &VoiceInput::error, this, [this](const QString &message) {
        m_composer->setDictationActive(false);
        statusBar()->showMessage(tr("Dictation failed: %1").arg(message), 10000);
    });
    connect(&m_sttRegistry, &SttRegistry::changed, this, [this]() { configureVoiceInput(); });
    connect(&m_ttsRegistry, &TtsRegistry::changed, this, [this]() { configureTts(); });
    connect(&m_ttsPlayer, &TtsPlayer::speakingChanged, this, [this](bool speaking) {
        QString message;
        int timeout = 5000;
        if (speaking) {
            message = tr("Speaking\u2026");
            timeout = 0;
        } else if (m_streaming) {
            message = tr("Generating\u2026");
        } else {
            message = tr("Ready");
        }
        statusBar()->showMessage(message, timeout);
    });
    connect(&m_ttsPlayer, &TtsPlayer::error, this, [this](const QString &message) {
        statusBar()->showMessage(tr("Text-to-speech failed: %1").arg(message), 10000);
    });
    connect(m_speakButton, &QPushButton::clicked, this, &MainWindow::onSpeakResponsesToggled);
    connect(m_chatView, &WebChatView::olderMessagesRequested, this,
            [this]() { loadOlderMessages(m_currentSession); });
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        if (m_composer->isEmpty())
            return;
        onSend(m_composer->composedText(), m_composer->attachments(), m_composer->documents());
    });
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_modelPicker, &ModelPickerButton::backendSelected, this, [this](const QString &id) {
        if (m_currentSession && !id.isEmpty()) {
            m_currentSession->setBackendId(id);
            syncSessionToDatabase(m_currentSession);
            disconnectBackend();
        }
        detectModels();
    });
    connect(m_modelPicker, &ModelPickerButton::modelSelected, this,
            [this](const QString &backendId, const QString &model) {
                onModelSelected(backendId, model);
                if (m_currentSession) {
                    m_currentSession->setModel(model.trimmed());
                    syncSessionToDatabase(m_currentSession);
                }
            });
    connect(&m_registry, &BackendRegistry::changed, this, &MainWindow::onBackendChanged);
    connect(&m_mcpManager, &McpManager::toolsChanged, this, &MainWindow::onMcpToolsChanged);
    connect(&m_mcpManager, &McpManager::permissionRequested, this,
            &MainWindow::onMcpPermissionRequested);
    connect(&m_mcpManager, &McpManager::serverLog, this,
            [this](const QString &serverName, const QString &level, const QString &message) {
                statusBar()->showMessage(tr("MCP [%1] %2: %3").arg(serverName, level, message),
                                         10000);
            });
    connect(&m_mcpManager, &McpManager::serverStateChanged, this,
            [this](const QString &, const QString &serverName, bool connected) {
                statusBar()->showMessage(
                    connected ? tr("MCP server \"%1\" connected.").arg(serverName)
                              : tr("MCP server \"%1\" disconnected.").arg(serverName),
                    5000);
                onMcpToolsChanged();
            });
    connect(&m_mcpManager, &McpManager::toolResultReady, this, &MainWindow::onToolResultReady);
    connect(&m_mcpManager, &McpManager::toolCallFailed, this,
            [this](const QString &toolName, const QString &error) {
                statusBar()->showMessage(tr("MCP tool \"%1\" failed: %2").arg(toolName, error),
                                         10000);
            });
}

MainWindow::~MainWindow() {
    qDeleteAll(m_sessions);
}

void MainWindow::onNewChat() {
    // Continue with the backend and model of the current chat, so a new
    // chat does not silently fall back to the first backend / another
    // provider's persisted model.
    const QString backendId = m_currentSession ? m_currentSession->backendId() : QString();
    const QString model = m_currentSession ? m_currentSession->model() : QString();
    createSession(backendId, model);
}

void MainWindow::createSession(const QString &backendId, const QString &model) {
    QString effectiveBackendId = backendId;
    if (effectiveBackendId.isEmpty()) {
        const auto enabled = m_registry.enabled();
        if (!enabled.isEmpty())
            effectiveBackendId = enabled.first().id.toString();
    }

    auto *session = new ChatSession(tr("New chat"), effectiveBackendId, this);
    QString effectiveModel = model;
    if (effectiveModel.isEmpty())
        effectiveModel = lastModelForBackend(effectiveBackendId);
    if (!effectiveModel.isEmpty())
        session->setModel(effectiveModel);
    // The session starts empty and every message is persisted live, so the
    // database already holds all of them; initialize the pagination counter
    // to the live message count (0 here) so loadOlderMessages() never
    // re-loads messages the session already has.
    session->setProperty("ocapi_loadedCount", 0);

    // Auto-generate a title from the first user message.
    connect(session, &ChatSession::messageAdded, this, [this, session](const Message &message) {
        if (message.role == Role::User && session->title() == tr("New chat")) {
            QString title = message.content;
            title.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (title.size() > 40)
                title = title.left(40) + QStringLiteral("…");
            session->setTitle(title);
            refreshSidebar();
        }
    });
    connect(session, &ChatSession::messageAdded, this, [this, session](const Message &message) {
        persistMessageForSession(session, message);
    });

    m_sessions.push_back(session);
    persistSession(session);
    connect(session, &ChatSession::titleChanged, this,
            [this, session](const QString &) { syncSessionToDatabase(session); });
    refreshSidebar();
    onSessionSelected(session->id());
}

void MainWindow::onSessionSelected(const QUuid &id) {
    disconnectBackend();
    m_currentSession = nullptr;
    for (auto *session : m_sessions) {
        if (session->id() == id) {
            m_currentSession = session;
            break;
        }
    }
    m_chatView->setSession(m_currentSession);
    m_sidebar->selectSession(id);

    refreshBackendSelector();
    detectModels();

    const bool hasSession = m_currentSession != nullptr;
    m_composer->setEnabled(hasSession);
    m_sendButton->setEnabled(hasSession);
    m_modelPicker->setEnabled(hasSession);
    updateEmptyState();
}

void MainWindow::onSessionDeleteRequested(const QUuid &id) {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if ((*it)->id() == id) {
            if (*it == m_currentSession) {
                disconnectBackend();
                m_currentSession = nullptr;
                m_chatView->setSession(nullptr);
            }
            if (m_database.isOpen())
                m_database.deleteSession((*it)->id().toString(QUuid::WithoutBraces));
            delete *it;
            m_sessions.erase(it);
            break;
        }
    }
    refreshSidebar();
    if (m_sessions.isEmpty())
        createSession();
    else if (!m_currentSession)
        onSessionSelected(m_sessions.first()->id());
    else
        updateEmptyState();
}

void MainWindow::openSettings(const QString &pageId) {
    SettingsDialog dialog(&m_registry, &m_mcpRegistry, &m_mcpManager, &m_sttRegistry,
                          &m_ttsRegistry, this);
    if (!pageId.isEmpty())
        dialog.selectPage(pageId);
    dialog.exec();
}

void MainWindow::loadMcpServersFromDatabase() {
    if (m_database.isOpen())
        m_mcpRegistry.load(m_database);
}

void MainWindow::loadSttSettingsFromDatabase() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_sttRegistry.loadSettings(settings);
}

void MainWindow::loadTtsSettingsFromDatabase() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_ttsRegistry.loadSettings(settings);
}

void MainWindow::loadLastModels() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    const auto keys = settings.allKeys();
    for (const QString &key : keys) {
        constexpr auto prefix = "models/last/";
        if (!key.startsWith(QString::fromLatin1(prefix)))
            continue;
        const QString backendId = key.mid(QString::fromLatin1(prefix).size());
        const QString model = settings.value(key).toString().trimmed();
        if (!backendId.isEmpty() && !model.isEmpty())
            m_lastModels.insert(backendId, model);
    }
}

void MainWindow::configureTts() {
    const auto services = m_ttsRegistry.enabled();
    QString activeId = m_ttsRegistry.activeServiceId();
    const TtsServiceConfig *active = nullptr;
    for (const auto &service : services) {
        if (service.id.toString(QUuid::WithoutBraces) == activeId) {
            active = &service;
            break;
        }
    }
    if (!active && !services.isEmpty())
        active = &services.first();
    if (active) {
        // A changed speed/voice on the same service must reach the player
        // without recreating it (setService would stop the running speech);
        // a different active service still needs a full swap.
        if (m_ttsPlayer.hasService() && m_ttsPlayer.serviceId() == active->id)
            m_ttsPlayer.updateService(*active);
        else
            m_ttsPlayer.setService(*active);
    }
    m_ttsPlayer.setOutputDevice(m_ttsRegistry.outputDevice());
    const bool available = active != nullptr;
    const bool enabled = available && m_ttsRegistry.speakResponses();
    m_speakButton->setVisible(available);
    const QSignalBlocker blocker(m_speakButton);
    m_speakButton->setChecked(enabled);
    if (!enabled)
        m_ttsPlayer.stop();
}

void MainWindow::onSpeakResponsesToggled() {
    const bool enabled = !m_ttsRegistry.speakResponses();
    m_ttsRegistry.setSpeakResponses(enabled);
    if (!enabled)
        m_ttsPlayer.stop();
}

void MainWindow::configureVoiceInput() {
    const auto services = m_sttRegistry.enabled();
    // Dictation is only offered when a speech-to-text service exists.
    if (m_composer)
        m_composer->setDictationAvailable(!services.isEmpty());
    QString activeId = m_sttRegistry.activeServiceId();
    const SttServiceConfig *active = nullptr;
    for (const auto &service : services) {
        if (service.id.toString(QUuid::WithoutBraces) == activeId) {
            active = &service;
            break;
        }
    }
    if (!active && !services.isEmpty())
        active = &services.first();
    if (active)
        m_voiceInput.setService(*active);
    m_voiceInput.setInputDevice(m_sttRegistry.inputDevice());
}

bool MainWindow::openDatabase() {
    if (!m_database.open(defaultDatabasePath()))
        return false;
    // Sessions come back ordered by last activity (newest first); keep that
    // order so the sidebar's time groups list recent chats first.
    for (const auto &stored : m_database.loadSessions()) {
        const QUuid id(stored.id);
        if (id.isNull())
            continue;
        auto *session = new ChatSession(id, stored.title, stored.backendId, this);
        session->setModel(stored.model);
        session->setCreatedAt(stored.createdAt);
        session->setLastActivityAt(stored.lastActivityAt);
        const int total = m_database.messageCount(stored.id);
        const int initial = qMin(total, kMessagesPerPage);
        for (const auto &message : m_database.messagesForSession(stored.id, initial, 0))
            appendStoredMessage(session, message);
        session->setProperty("ocapi_loadedCount", initial);
        connect(session, &ChatSession::messageAdded, this, [this, session](const Message &message) {
            persistMessageForSession(session, message);
        });
        m_sessions.push_back(session);
    }
    return true;
}

void MainWindow::loadBackendsFromDatabase() {
    if (m_database.isOpen())
        m_registry.load(m_database);
}

void MainWindow::loadOlderMessages(ChatSession *session) {
    if (!m_database.isOpen() || !session || m_loadingOlder)
        return;
    const QString sessionId = session->id().toString(QUuid::WithoutBraces);
    const int total = m_database.messageCount(sessionId);
    // Sessions created live persist every message as it arrives, so the
    // database count includes messages the session already holds. The
    // pagination offset must account for the live tail: messages already in
    // the session are the newest ones, and the offset skips exactly that
    // many rows from the top (the query returns newest-first).
    const int liveCount = static_cast<int>(session->messages().size());
    const int loaded = session->property("ocapi_loadedCount").toInt() + liveCount;
    if (loaded >= total)
        return;
    m_loadingOlder = true;
    const int pageSize = qMin(kMessagesPerPage, total - loaded);
    QVector<Message> older;
    for (const auto &stored : m_database.messagesForSession(sessionId, pageSize, loaded)) {
        Message message = storedToMessage(stored);
        // Skip messages the session already holds (e.g. after a live stream
        // that raced a page load); prepending them would duplicate rows.
        if (!message.id.isNull() &&
            std::ranges::none_of(session->messages(),
                                 [&message](const Message &m) { return m.id == message.id; }))
            older.push_back(message);
    }
    if (!older.isEmpty()) {
        session->prependMessages(older);
        m_chatView->prependMessages(older);
        // Track how many DB rows sit above the live tail: previously paged-in
        // history plus the live messages themselves.
        session->setProperty("ocapi_loadedCount", session->property("ocapi_loadedCount").toInt() +
                                                      liveCount + static_cast<int>(older.size()));
    }
    m_loadingOlder = false;
}

void MainWindow::persistSession(ChatSession *session) {
    if (!m_database.isOpen())
        return;
    m_database.insertSession(*session);
}

void MainWindow::persistMessageForSession(ChatSession *session, const Message &message) {
    if (!m_database.isOpen() || !session)
        return;
    m_database.insertMessage(session->id().toString(QUuid::WithoutBraces),
                             message.id.toString(QUuid::WithoutBraces), message.roleName(),
                             message.content, message.timestamp, message.images, message.files,
                             message.toolCalls, message.reasoningSegments, message.elementOrder);
    // Keep the session's last-activity timestamp fresh so the sidebar's
    // time groups stay current, then rebuild the tree to re-sort/re-group.
    session->setLastActivityAt(message.timestamp.isValid() ? message.timestamp
                                                           : QDateTime::currentDateTime());
    m_database.updateSession(*session);
    if (m_sessions.size() > 1)
        refreshSidebar();
}

void MainWindow::syncSessionToDatabase(ChatSession *session) {
    if (!m_database.isOpen() || !session)
        return;
    m_database.updateSession(*session);
}

void MainWindow::onBackendChanged() {
    // Drop persisted model selections of backends that no longer exist.
    QSet<QString> existing;
    for (const auto &config : m_registry.backends())
        existing.insert(config.id.toString());
    for (auto it = m_lastModels.begin(); it != m_lastModels.end();) {
        if (!existing.contains(it.key()))
            it = m_lastModels.erase(it);
        else
            ++it;
    }
    m_pendingDetection.clear();
    refreshBackendSelector();
    updateEmptyState();
    detectModels();
}

QStringList MainWindow::knownModelsForBackend(const BackendConfig &config) const {
    QStringList list;
    const QString last = m_lastModels.value(config.id.toString());
    if (!last.isEmpty())
        list.push_back(last);
    for (const QString &detected : m_detectedModels.value(config.id.toString()))
        if (!list.contains(detected))
            list.append(detected);
    return list;
}

QString MainWindow::lastModelForBackend(const QString &backendId) const {
    return m_lastModels.value(backendId);
}

void MainWindow::saveLastModelForBackend(const QString &backendId, const QString &model) {
    if (backendId.isEmpty() || model.trimmed().isEmpty())
        return;
    if (m_lastModels.value(backendId) == model.trimmed())
        return;
    m_lastModels.insert(backendId, model.trimmed());
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("models/last/%1").arg(backendId), model.trimmed());
}

void MainWindow::forgetLastModelForBackend(const QString &backendId) {
    if (!m_lastModels.remove(backendId))
        return;
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    settings.remove(QStringLiteral("models/last/%1").arg(backendId));
}

void MainWindow::refreshSidebar() {
    // Sessions are kept sorted by last activity (newest first) so the
    // sidebar's time groups list recent chats at the top.
    std::ranges::sort(m_sessions, [](const ChatSession *a, const ChatSession *b) {
        return a->lastActivityAt() > b->lastActivityAt();
    });
    m_sidebar->setSessions(m_sessions);
    if (m_currentSession)
        m_sidebar->selectSession(m_currentSession->id());
}

void MainWindow::refreshBackendSelector() {
    QVector<QPair<QString, QString>> backends;
    QHash<QString, QStringList> models;
    for (const auto &config : m_registry.backends()) {
        const QString name = config.enabled ? config.name : tr("%1 (disabled)").arg(config.name);
        backends.push_back(qMakePair(name, config.id.toString()));
        models.insert(config.id.toString(), knownModelsForBackend(config));
    }

    QString selectedId = m_currentSession ? m_currentSession->backendId() : QString();
    if (selectedId.isEmpty() && !backends.isEmpty())
        selectedId = backends.first().second;

    QString model = m_currentSession ? m_currentSession->model() : QString();
    if (model.isEmpty())
        model = lastModelForBackend(selectedId);
    if (model.isEmpty()) {
        // Nothing persisted for this backend: take the first detected model,
        // so the picker shows a model as soon as detection completed.
        const QStringList detected = m_detectedModels.value(selectedId);
        if (!detected.isEmpty())
            model = detected.first();
    }
    if (m_currentSession && !model.isEmpty())
        m_currentSession->setModel(model);

    QString backendName;
    for (const auto &entry : backends) {
        if (entry.second == selectedId)
            backendName = entry.first;
    }

    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS")) {
        QStringList perBackend;
        for (auto it = models.begin(); it != models.end(); ++it)
            perBackend << it.key() + QLatin1Char(':') + QString::number(it.value().size());
        qWarning().noquote() << "[ocapi models] refreshBackendSelector selected=" << selectedId
                             << "model=" << model << "backends=" << backends.size()
                             << "modelLists=" << perBackend.join(QLatin1Char(' '));
    }
    const QSignalBlocker blocker(m_modelPicker);
    m_modelPicker->setBackends(backends, models);
    m_modelPicker->setBackendAndModel(backendName, selectedId, model);
    if (m_currentSession && !selectedId.isEmpty())
        m_currentSession->setBackendId(selectedId);
}

void MainWindow::addModelsToPicker(const QString &backendId, const QStringList &models) {
    const QSignalBlocker blocker(m_modelPicker);
    m_modelPicker->addModels(backendId, models);
}

void MainWindow::onModelSelected(const QString &backendId, const QString &model) {
    if (m_currentSession && !backendId.isEmpty())
        m_currentSession->setBackendId(backendId);
    if (m_currentSession)
        m_currentSession->setModel(model.trimmed());
    // Persist the selection per backend until the user picks another model
    // or the backend is removed.
    saveLastModelForBackend(backendId, model);
}

void MainWindow::detectModels() {
    // Query every enabled backend, not just the selected one: the picker
    // shows a submenu per provider, and a provider the user never selected
    // would otherwise stay at "No models detected yet".
    for (const auto &config : m_registry.backends()) {
        if (!config.enabled)
            continue;
        const QString backendId = config.id.toString();
        if (!m_detectedModels.value(backendId).isEmpty())
            continue; // already detected successfully for this backend
        if (m_pendingDetection.contains(backendId))
            continue; // request already in flight
        m_pendingDetection.insert(backendId);
        auto *detector = createBackend(config).release();
        detector->setParent(this);
        const QString url = config.effectiveBaseUrl();
        if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
            qWarning().noquote() << "[ocapi models] detectModels() sending listModels() for"
                                 << config.name << backendId << "to" << url;
        connect(detector, &IBackend::modelsReady, this,
                [this, backendId, detector](const QStringList &models) {
                    if (m_detectors.contains(backendId) && m_detectors.value(backendId) == detector)
                        onModelsDetected(backendId, models);
                });
        connect(detector, &IBackend::modelsFailed, this,
                [this, backendId, detector](const QString &error) {
                    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
                        qWarning().noquote()
                            << "[ocapi models] detection FAILED for" << backendId << ":" << error;
                    if (m_detectors.find(backendId) != m_detectors.end() &&
                        *m_detectors.find(backendId) == detector) {
                        m_pendingDetection.remove(backendId); // allow retry on next trigger
                        m_detectors.remove(backendId);
                        detector->deleteLater();
                    }
                });
        m_detectors.insert(backendId, detector);
        detector->listModels();
    }
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS")) {
        QStringList pending;
        for (const QString &id : m_pendingDetection)
            pending << id;
        qWarning().noquote() << "[ocapi models] detectModels() done; pending="
                             << pending.join(QLatin1Char(' '));
    }
}

void MainWindow::onModelsDetected(const QString &backendId, const QStringList &models) {
    m_pendingDetection.remove(backendId);
    const auto detIt = m_detectors.find(backendId);
    if (detIt != m_detectors.end()) {
        (*detIt)->deleteLater();
        m_detectors.erase(detIt);
    }
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_MODELS"))
        qWarning().noquote() << "[ocapi models] onModelsDetected backend=" << backendId
                             << "count=" << models.size() << "models=" << models;
    if (models.isEmpty())
        return;
    if (!m_registry.find(backendId))
        return;
    QStringList cached = m_detectedModels.value(backendId);
    for (const QString &model : models)
        if (!cached.contains(model))
            cached.append(model);
    m_detectedModels.insert(backendId, cached);
    // Rebuild the whole selector so the model lists (and the persisted
    // last-model entry) reach the picker even if only addModels() ran before.
    refreshBackendSelector();
    // If no model is selected for the current backend yet (fresh backend,
    // nothing persisted), take the first detected one so the picker shows
    // "Backend | model".
    if (backendId == m_modelPicker->currentBackendId() && m_modelPicker->currentModel().isEmpty() &&
        !cached.isEmpty()) {
        const QString first = cached.first();
        m_modelPicker->setBackendAndModel(m_modelPicker->backendName(), backendId, first);
        if (m_currentSession)
            m_currentSession->setModel(first);
        saveLastModelForBackend(backendId, first);
    }
}

double MainWindow::throughputFor(const QUuid &messageId) {
    if (messageId != m_streamedMessageId || m_streamedTokens <= 0 || !m_firstToken.isValid())
        return -1.0;
    const qint64 elapsedMs = m_firstToken.msecsTo(QDateTime::currentDateTime());
    if (elapsedMs <= 0)
        return -1.0;
    return m_streamedTokens * 1000.0 / static_cast<double>(elapsedMs);
}

void MainWindow::onSend(const QString &text, const QVector<ImageAttachment> &images,
                        const QVector<FileAttachment> &files) {
    if (!m_currentSession || m_streaming || m_runningTools)
        return;
    if (text.isEmpty() && images.isEmpty() && files.isEmpty())
        return;

    const QString backendId = m_currentSession->backendId();
    auto *config = m_registry.find(backendId);
    if (!config) {
        QMessageBox::warning(
            this, tr("No backend"),
            tr("Please configure a backend first via \"Settings… → Model backends\"."));
        return;
    }
    if (!config->enabled) {
        QMessageBox::warning(this, tr("Backend disabled"),
                             tr("The backend \"%1\" is disabled.").arg(config->name));
        return;
    }

    QString model = m_currentSession->model();
    if (model.isEmpty())
        model = m_modelPicker->currentModel().trimmed();
    if (model.isEmpty()) {
        QMessageBox::warning(this, tr("No model"),
                             tr("Please select a model via the model picker above the input."));
        return;
    }
    m_currentSession->setModel(model);

    Message userMessage;
    userMessage.role = Role::User;
    userMessage.content = text;
    userMessage.images = images;
    userMessage.files = files;
    m_currentSession->appendMessage(userMessage);
    m_composer->clearText();
    m_composer->clearAttachments();

    m_backend = createBackend(*config);
    if (m_backend->supportsTools())
        m_backend->setAvailableTools(m_mcpManager.availableTools());
    m_streamedTokens = 0;
    m_firstToken = QDateTime();
    connectBackend(m_backend.get());
    m_streaming = true;
    m_composer->setBusy(true);
    m_sendButton->setVisible(false);
    m_stopButton->setVisible(true);
    statusBar()->showMessage(tr("Generating…"));

    const qsizetype count = m_currentSession->messages().size();
    m_backend->sendMessage(m_currentSession, model);
    // The backend appends the assistant message inside sendMessage().
    if (m_currentSession->messages().size() > count) {
        const Message &assistant = m_currentSession->messages().constLast();
        if (assistant.role == Role::Assistant) {
            m_streamedMessageId = assistant.id;
            m_turnAssistantMessageId = assistant.id;
        }
    }
}

void MainWindow::onStop() {
    m_ttsPlayer.stop();
    if (m_backend)
        m_backend->cancel();
}

IBackend *MainWindow::activeBackend() {
    return m_backend.get();
}

void MainWindow::connectBackend(IBackend *backend) {
    connect(backend, &IBackend::toolCallsRequested, this, &MainWindow::onToolCallsRequested);
    connectStreamingSignals(backend);
}

void MainWindow::connectStreamingSignals(IBackend *backend) const {
    connect(backend, &IBackend::tokenReceived, this, &MainWindow::onTokenReceived);
    connect(backend, &IBackend::reasoningReceived, this, &MainWindow::onReasoningReceived);
    connect(backend, &IBackend::finished, this, &MainWindow::onStreamFinished);
    connect(backend, &IBackend::failed, this, &MainWindow::onStreamFailed);
}

void MainWindow::onTokenReceived(const QUuid &messageId, const QString &text) {
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_SSE"))
        qWarning().noquote() << "[ocapi debug] tokenReceived" << messageId.toString()
                             << "len:" << text.size() << "text:" << text.left(200);
    if (messageId == m_streamedMessageId) {
        ++m_streamedTokens;
        if (!m_firstToken.isValid())
            m_firstToken = QDateTime::currentDateTime();
    }
    if (m_currentSession) {
        m_currentSession->updateMessage(messageId, text, true);
        // The first visible answer token ends the reasoning phase.
        m_currentSession->setReasoningStreaming(messageId, false);
    }
    if (m_ttsRegistry.speakResponses())
        m_ttsPlayer.feedText(text);
    if (m_database.isOpen())
        m_database.updateMessage(messageId.toString(QUuid::WithoutBraces), text, true);
}

void MainWindow::onReasoningReceived(const QUuid &messageId, const QString &text) {
    // Reasoning streams before the visible answer; keep the message in the
    // reasoning-streaming state until the answer or the stream end arrives.
    if (m_currentSession) {
        m_currentSession->updateReasoning(messageId, text, true);
        if (messageId == m_streamedMessageId) {
            ++m_streamedTokens;
            if (!m_firstToken.isValid())
                m_firstToken = QDateTime::currentDateTime();
        }
    }
    if (m_database.isOpen()) {
        // Persist all thinking phases of the message, not just the current one,
        // plus the element order so the chat renders thinking/tool calls in
        // the sequence they happened after a reload.
        QStringList segments;
        QStringList elementOrder;
        if (m_currentSession) {
            for (const auto &message : m_currentSession->messages()) {
                if (message.id == messageId) {
                    segments = message.reasoningSegments;
                    elementOrder = message.elementOrder;
                    break;
                }
            }
        }
        m_database.setReasoning(messageId.toString(QUuid::WithoutBraces), segments);
        m_database.setElementOrder(messageId.toString(QUuid::WithoutBraces), elementOrder);
    }
}

void MainWindow::onStreamFinished(const QUuid &messageId) {
    const QString content = messageContent(messageId);
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_SSE"))
        qWarning().noquote() << "[ocapi debug] streamFinished" << messageId.toString()
                             << "len:" << content.size() << "content:" << content.left(300);
    if (m_currentSession) {
        m_currentSession->updateMessage(messageId, content, false);
        m_currentSession->setReasoningStreaming(messageId, false);
    }
    if (m_ttsRegistry.speakResponses())
        m_ttsPlayer.endOfText();
    const double tokensPerSecond = throughputFor(messageId);
    if (tokensPerSecond > 0.0 && m_currentSession)
        m_currentSession->setThroughput(messageId, tokensPerSecond);
    if (m_database.isOpen()) {
        m_database.updateMessage(messageId.toString(QUuid::WithoutBraces), content, false);
        if (tokensPerSecond > 0.0)
            m_database.setThroughput(messageId.toString(QUuid::WithoutBraces), tokensPerSecond);
        syncSessionToDatabase(m_currentSession);
    }
    endStreaming(tr("Ready"));
    m_composer->focusInput();
}

void MainWindow::onStreamFailed(const QUuid &messageId, const QString &error) {
    if (m_currentSession)
        m_currentSession->failMessage(messageId, error);
    m_ttsPlayer.stop();
    if (m_database.isOpen())
        m_database.failMessage(messageId.toString(QUuid::WithoutBraces), error);
    endStreaming(tr("Error: %1").arg(error));
}

QString MainWindow::messageContent(const QUuid &messageId) const {
    if (!m_currentSession)
        return {};
    for (const auto &message : m_currentSession->messages())
        if (message.id == messageId)
            return message.content;
    return {};
}

void MainWindow::endStreaming(const QString &statusMessage) {
    m_streaming = false;
    if (m_ttsRegistry.speakResponses())
        m_ttsPlayer.endOfText();
    m_composer->setBusy(false);
    m_sendButton->setVisible(true);
    m_stopButton->setVisible(false);
    statusBar()->showMessage(statusMessage);
}

void MainWindow::disconnectBackend() {
    m_ttsPlayer.stop();
    if (m_backend) {
        m_backend->cancel();
        m_backend->disconnect(this);
        m_backend.reset();
    }
    m_streaming = false;
    m_composer->setBusy(false);
    m_sendButton->setVisible(true);
    m_stopButton->setVisible(false);
}

void MainWindow::onToolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls) {
    if (toolDebug::enabled()) {
        QJsonArray calls;
        for (const auto &call : toolCalls) {
            calls.append(QJsonObject{
                {QStringLiteral("id"), call.id},
                {QStringLiteral("name"), call.name},
                {QStringLiteral("arguments"), call.arguments},
            });
        }
        toolDebug::log(
            QStringLiteral("toolCallsRequested: messageId=%1 calls=%2")
                .arg(messageId.toString(QUuid::WithoutBraces),
                     QString::fromUtf8(QJsonDocument(calls).toJson(QJsonDocument::Compact))));
    }
    if (!m_currentSession || toolCalls.isEmpty())
        return;
    // Record the requested tool calls on the assistant message so the chat UI
    // can render them (collapsed, with a progress indicator) and the
    // conversation history stays complete. Tool-call rounds continue the same
    // assistant message, so append to the calls earlier rounds requested to
    // keep them in call order. Backends that do not provide a tool-call id
    // (e.g. Ollama) get a generated one: the id keys the chat element and
    // matches the finished status update to the right call.
    QVector<ToolCall> calls = toolCalls;
    for (auto &call : calls) {
        if (call.id.isEmpty())
            call.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_currentSession->appendToolCalls(messageId, calls);
    persistToolCalls(m_currentSession, messageId);
    runToolCalls(m_currentSession, messageId, calls);
}

void MainWindow::persistToolCalls(ChatSession *session, const QUuid &messageId) {
    if (!m_database.isOpen() || !session)
        return;
    const Message *message = nullptr;
    for (const auto &candidate : session->messages()) {
        if (candidate.id == messageId) {
            message = &candidate;
            break;
        }
    }
    if (!message)
        return;
    m_database.insertMessage(session->id().toString(QUuid::WithoutBraces),
                             messageId.toString(QUuid::WithoutBraces), message->roleName(),
                             message->content, message->timestamp, message->images, message->files,
                             message->toolCalls, message->reasoningSegments, message->elementOrder);
}

void MainWindow::onToolResultReady(const QString &toolName, const QString &resultText,
                                   bool isError) {
    if (!m_currentSession)
        return;
    // Mark the matching tool call finished on the assistant message that
    // requested it; the chat repaints the element without the spinner.
    for (const auto &message : m_currentSession->messages()) {
        for (const auto &call : message.toolCalls) {
            if (call.name == toolName && call.status == ToolCallStatus::Running) {
                m_currentSession->setToolCallStatus(
                    message.id, call.id, isError ? ToolCallStatus::Failed : ToolCallStatus::Done,
                    resultText);
                if (m_database.isOpen())
                    m_database.updateToolCallStatus(
                        message.id.toString(QUuid::WithoutBraces), call.id,
                        isError ? ToolCallStatus::Failed : ToolCallStatus::Done, resultText);
                return;
            }
        }
    }
    statusBar()->showMessage(isError ? tr("MCP tool \"%1\" failed.").arg(toolName)
                                     : tr("MCP tool \"%1\" finished.").arg(toolName),
                             5000);
}

void MainWindow::runToolCalls(ChatSession *session, const QUuid &assistantMessageId,
                              const QVector<ToolCall> &toolCalls) {
    Q_UNUSED(assistantMessageId)
    m_runningTools = true;
    m_toolSession = session;
    m_pendingToolRounds = static_cast<int>(toolCalls.size());
    m_composer->setBusy(true);
    statusBar()->showMessage(tr("Running MCP tools…"));
    // Watch for tool result messages; each result decrements the round count.
    auto *watcher = new QObject(this);
    connect(session, &ChatSession::messageAdded, watcher,
            [this, session, watcher](const Message &message) {
                if (message.role != Role::Tool)
                    return;
                watcher->deleteLater();
                --m_pendingToolRounds;
                if (m_pendingToolRounds <= 0)
                    continueAfterToolResults(session);
            });
    for (const auto &call : toolCalls) {
        // executeToolCall returns false when the user must be asked first; in
        // that case the round is accounted for when the permission is decided
        // (see onMcpPermissionRequested). Synchronous failures (unknown tool,
        // disabled tool/server) append a Role::Tool message immediately, which
        // the watcher above picks up.
        m_mcpManager.executeToolCall(session, call);
    }
    // When every call failed synchronously, the watcher already fired and the
    // loop continues via continueAfterToolResults. Calls that need a permission
    // decision resume in onMcpPermissionRequested.
}

void MainWindow::continueAfterToolResults(ChatSession *session) {
    // Defer to the next event-loop iteration: this can be reached synchronously
    // from within the backend's toolCallsRequested emission (a synchronously
    // failing tool call or a denied permission appends its tool result
    // immediately, and the watcher in runToolCalls then lands here). Replacing
    // m_backend below would destroy the emitting backend while its signal is
    // still being delivered, corrupting the heap.
    QTimer::singleShot(0, this, [this, session]() { sendConversationAfterToolResults(session); });
}

void MainWindow::sendConversationAfterToolResults(ChatSession *session) {
    if (!m_runningTools)
        return; // already continued
    if (!session || session != m_currentSession)
        return;
    if (toolDebug::enabled()) {
        toolDebug::log(QStringLiteral("sendConversationAfterToolResults: continuing message %1")
                           .arg(m_turnAssistantMessageId.toString(QUuid::WithoutBraces)));
        toolDebug::logSession(session, QStringLiteral("session before next round"));
    }
    m_runningTools = false;
    m_toolSession = nullptr;
    // Send the conversation (now including tool results) back to the model.
    const QString backendId = session->backendId();
    auto *config = m_registry.find(backendId);
    if (!config || !config->enabled) {
        endStreaming(tr("Ready"));
        return;
    }
    QString model = session->model();
    if (model.isEmpty())
        model = m_modelPicker->currentModel().trimmed();
    m_backend = createBackend(*config);
    if (m_backend->supportsTools())
        m_backend->setAvailableTools(m_mcpManager.availableTools());
    m_streamedTokens = 0;
    m_firstToken = QDateTime();
    connectBackend(m_backend.get());
    m_streaming = true;
    m_composer->setBusy(true);
    m_sendButton->setVisible(false);
    m_stopButton->setVisible(true);
    statusBar()->showMessage(tr("Generating…"));
    // Continue the same assistant message: the new round's thinking, tool
    // calls and answer text all belong to the one message of this turn.
    m_backend->sendMessage(session, model, m_turnAssistantMessageId);
    m_streamedMessageId = m_turnAssistantMessageId;
}

void MainWindow::onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                          const QString &toolName, const QJsonObject &arguments) {
    const QString args =
        QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented));
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Allow tool call?"));
    box.setText(tr(R"(MCP server "%1" wants to run the tool "%2".)").arg(serverName, toolName));
    box.setDetailedText(args);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    const bool allow = box.exec() == QMessageBox::Yes;
    // decidePermission either starts the call (result arrives via the session's
    // messageAdded signal) or appends a denial message synchronously.
    const int toolMessagesBefore = [this]() {
        int count = 0;
        if (m_toolSession)
            for (const auto &message : m_toolSession->messages())
                if (message.role == Role::Tool)
                    ++count;
        return count;
    }();
    m_mcpManager.decidePermission(callKey, allow);
    if (!allow && m_toolSession) {
        // Denied: a tool message was appended synchronously; account for it.
        int toolMessagesAfter = 0;
        for (const auto &message : m_toolSession->messages())
            if (message.role == Role::Tool)
                ++toolMessagesAfter;
        if (toolMessagesAfter > toolMessagesBefore) {
            --m_pendingToolRounds;
            if (m_pendingToolRounds <= 0)
                continueAfterToolResults(m_toolSession);
        }
    }
}

void MainWindow::onMcpToolsChanged() {
    const int toolCount = static_cast<int>(m_mcpManager.allTools().size());
    if (toolCount == 0)
        m_mcpStatusLabel->setText(tr("MCP: no tools"));
    else
        m_mcpStatusLabel->setText(tr("MCP: %n tool(s)", nullptr, toolCount));
}

void MainWindow::updateEmptyState() {
    const bool noBackends = m_registry.backends().isEmpty();
    const bool noSession = m_currentSession == nullptr;
    m_emptyLabel->setVisible(noBackends || noSession);
    m_chatView->setVisible(!m_emptyLabel->isVisible());
    if (noBackends)
        m_emptyLabel->setText(tr("No backends configured yet.\n\n"
                                 "Open \"Settings… → Model backends\" to add Ollama, llama.cpp,\n"
                                 "vLLM, OpenAI, Anthropic, Google Gemini, Mistral AI or any\n"
                                 "OpenAI-compatible endpoint (local or remote)."));
    else if (noSession)
        m_emptyLabel->setText(tr("Select a chat or start a new one."));
    const bool enabled = !noSession && !noBackends;
    m_composer->setEnabled(enabled);
    // The mic/attach buttons live in the picker row, not inside the composer.
    m_composer->micButton()->setEnabled(enabled && !m_composer->isBusy());
    m_composer->attachButton()->setEnabled(enabled && !m_composer->isBusy());
    m_sendButton->setEnabled(!noSession && !noBackends);
}

} // namespace ocapi
