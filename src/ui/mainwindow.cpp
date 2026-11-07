#include "ui/mainwindow.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/attachmentopener.h"
#include "core/chatsession.h"
#include "core/tooldebug.h"
#include "storage/chatdatabase.h"
#include "ui/composer.h"
#include "ui/settingsdialog.h"
#include "ui/sidebar.h"
#include "ui/webchatview.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShortcut>
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
// Minimum height of the chat history area when the composer splitter is
// dragged; the composer column minimum is UiSettings::kMinComposerHeight.
constexpr int kMinChatHeight = 120;

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

// Per-chat state of one in-flight model turn: the backend instance streaming
// it and the bookkeeping the stream handlers need. Chats run in parallel, so
// every value that used to be a singleton in the main window lives here now.
struct StreamContext {
    ChatSession *session = nullptr;
    std::unique_ptr<IBackend> backend;
    bool streaming = false;
    bool runningTools = false; // a tool-call round is in progress
    int pendingToolRounds = 0;
    QUuid streamedMessageId;
    int streamedTokens = 0;
    QDateTime firstToken;
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_mcpManager(&m_mcpRegistry) {
    setWindowTitle(tr("Ocapi"));
    resize(1100, 720);

    if (!openDatabase())
        statusBar()->showMessage(tr("Warning: chat storage unavailable, chats will not be "
                                    "persisted."));
    loadBackendsFromDatabase();
    loadMcpServersFromDatabase();
    loadSttSettingsFromDatabase();
    loadSystemPrompt();
    loadLastModels();
    loadShortcuts();
    loadUiSettings();
    loadPromptTemplates();

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
    applyComposerHeight();
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
    m_newChatButton->setText(tr("New"));
    m_newChatButton->setIcon(
        QIcon(QString::fromUtf8(":/resources/icons/contact-new-symbolic.svg")));
    // Widgets added via addWidget() don't inherit the toolbar's
    // toolButtonStyle; set it explicitly so icon and text both show.
    m_newChatButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const QKeySequence newChatSequence = m_shortcuts.sequence(QStringLiteral("newChat"));
    m_newChatButton->setToolTip(
        tr("Start a new chat (%1)").arg(newChatSequence.toString(QKeySequence::NativeText)));
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
    m_emptyLabel =
        new QLabel(tr("Select a chat or start a new one.\n\n"
                      "Set up a backend via \"Settings… → Model backends\" to get started."),
                   rightPanel);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: gray;"));
    chatMargin->addWidget(m_emptyLabel);
    m_emptyLabel->hide();
    auto *chatMarginHost = new QWidget(rightPanel);
    chatMarginHost->setLayout(chatMargin);
    chatMarginHost->setMinimumHeight(kMinChatHeight);

    auto *composerColumn = new QWidget(rightPanel);
    auto *composerColumnLayout = new QVBoxLayout(composerColumn);
    composerColumnLayout->setContentsMargins(kPanelMargin, kPanelMargin, kPanelMargin,
                                             kPanelMargin);
    composerColumnLayout->setSpacing(4);
    m_composer = new Composer(composerColumn);
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
    // Shown only while a user message is being edited.
    m_cancelEditButton = new QPushButton(tr("Cancel"), footerRow);
    m_cancelEditButton->setVisible(false);
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
    footerLayout->addWidget(m_cancelEditButton);
    footerLayout->addWidget(m_sendButton);
    footerLayout->addWidget(m_stopButton);
    composerColumnLayout->addWidget(footerRow);
    composerColumn->setMinimumHeight(UiSettings::kMinComposerHeight);
    m_composerColumn = composerColumn;

    // The chat area and the composer share a vertical splitter, so the
    // composer height is resizable with the mouse (persisted in the UI
    // settings); attachment thumbnails grow the composer column on top of
    // the persisted base height, shrinking the chat area.
    m_rightSplitter = new QSplitter(Qt::Vertical, rightPanel);
    m_rightSplitter->setChildrenCollapsible(false);
    m_rightSplitter->addWidget(chatMarginHost);
    m_rightSplitter->addWidget(composerColumn);
    rightLayout->addWidget(m_rightSplitter, 1);

    // Dropping files/images onto the chat view forwards them to the
    // composer's attachment strip.
    connect(m_chatView, &WebChatView::urlsDropped, m_composer, &Composer::addDroppedUrls);
    connect(m_chatView, &WebChatView::imageDropped, m_composer, &Composer::addDroppedImage);

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
    createWindowShortcuts();
    m_composer->setDictateSequence(m_shortcuts.sequence(QStringLiteral("dictate")));
    m_composer->setPromptTriggerSequence(m_shortcuts.sequence(QStringLiteral("promptTemplates")));
    connect(m_composer, &Composer::promptTemplateRequested, this,
            &MainWindow::showPromptTemplateMenu);
    connect(m_composer, &Composer::promptFilterChanged, this, &MainWindow::onPromptFilterChanged);
    connect(m_composer, &Composer::promptMenuConfirmed, this, [this]() { confirmPromptMenu(); });
    connect(m_composer, &Composer::promptMenuClosed, this, &MainWindow::closePromptMenu);
    // Close the popup when the focus leaves composer and popup.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *now) {
        if (m_promptMenu && m_promptMenu->isVisible() && now != m_composer &&
            !m_promptMenu->isAncestorOf(now))
            closePromptMenu();
    });
    connect(&m_shortcuts, &ShortcutRegistry::changed, this, &MainWindow::reapplyShortcuts);
    m_chatView->setShowThroughput(m_uiSettings.showThroughput());
    connect(&m_uiSettings, &UiSettings::changed, this,
            [this]() { m_chatView->setShowThroughput(m_uiSettings.showThroughput()); });
    connect(m_settingsButton, &QToolButton::clicked, this, [this] { openSettings(QString()); });
    setupComposerSplitter();
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
        } else if (contextFor(m_currentSession)) {
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
    setupChatViewConnections();
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        if (m_composer->isEmpty())
            return;
        onSend(m_composer->composedText(), m_composer->attachments(), m_composer->documents());
    });
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_cancelEditButton, &QPushButton::clicked, this, &MainWindow::cancelEditMessage);
    connect(m_chatView, &WebChatView::messageEditRequested, this, &MainWindow::startEditMessage);
    connect(m_modelPicker, &ModelPickerButton::backendSelected, this, [this](const QString &id) {
        if (m_currentSession && !id.isEmpty()) {
            m_currentSession->setBackendId(id);
            syncSessionToDatabase(m_currentSession);
            // The running turn belongs to the previous backend; switching
            // the chat's backend ends it, as before the parallel-chats rework.
            cancelContext(m_currentSession);
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
    // A new chat is where the user starts typing: always move focus to the
    // input. Deferred to the next event-loop iteration so a focus grab by
    // the previously focused widget (e.g. the chat web view after a click
    // there) cannot steal the focus back afterwards.
    QTimer::singleShot(0, this, [this]() { m_composer->focusInput(); });
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
    if (!m_editingMessageId.isNull())
        cancelEditMessage();
    // Switching chats never interrupts a running turn: the context of the
    // previous chat keeps streaming in the background.
    m_currentSession = nullptr;
    for (auto *session : m_sessions) {
        if (session->id() == id) {
            m_currentSession = session;
            break;
        }
    }
    m_chatView->setSession(m_currentSession);
    m_sidebar->selectSession(id);

    // The user is looking at this chat now: its unread badge clears.
    if (m_currentSession && m_unreadSessions.remove(id))
        updateStreamingUi();

    refreshBackendSelector();
    detectModels();

    const bool hasSession = m_currentSession != nullptr;
    m_composer->setEnabled(hasSession);
    m_sendButton->setEnabled(hasSession);
    m_modelPicker->setEnabled(hasSession);
    updateStreamingUi();
    updateEmptyState();
}

void MainWindow::cycleChat(int direction) {
    if (m_sessions.isEmpty())
        return;
    // The sidebar lists chats sorted by last activity (newest first);
    // cycling walks that visual order and wraps around at both ends.
    std::ranges::sort(m_sessions, [](const ChatSession *a, const ChatSession *b) {
        return a->lastActivityAt() > b->lastActivityAt();
    });
    const int count = static_cast<int>(m_sessions.size());
    int index = 0;
    for (int i = 0; i < count; ++i) {
        if (m_sessions.at(i) == m_currentSession) {
            index = i;
            break;
        }
    }
    index = (index + direction + count) % count;
    onSessionSelected(m_sessions.at(index)->id());
}

void MainWindow::onSessionDeleteRequested(const QList<QUuid> &ids) {
    // Defense in depth: running and unread chats are never deleted, even if
    // the requester did not filter them out already.
    const QSet<QUuid> running = runningSessionIds();
    QList<QUuid> toDelete;
    for (const QUuid &id : ids) {
        if (!id.isNull() && !running.contains(id) && !m_unreadSessions.contains(id))
            toDelete.append(id);
    }
    if (toDelete.isEmpty())
        return;
    // Deleting several chats at once asks for confirmation first.
    if (toDelete.size() > 1) {
        const auto answer =
            QMessageBox::question(this, tr("Delete chats"),
                                  tr("Do you really want to delete %1 chats?").arg(toDelete.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    for (const QUuid &id : toDelete)
        deleteSession(id);
}

void MainWindow::deleteSession(const QUuid &id) {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if ((*it)->id() == id) {
            // A running turn of the deleted chat is cancelled with it.
            cancelContext(*it);
            if (*it == m_currentSession) {
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
    m_unreadSessions.remove(id);
    refreshSidebar();
    if (m_sessions.isEmpty())
        createSession();
    else if (!m_currentSession)
        onSessionSelected(m_sessions.first()->id());
    else
        updateEmptyState();
}

void MainWindow::setupChatViewConnections() {
    connect(m_chatView, &WebChatView::olderMessagesRequested, this,
            [this]() { loadOlderMessages(m_currentSession); });
    // Links in chat messages open in the default web browser (http/https
    // only); while hovering a link its URL is shown in the status bar.
    connect(m_chatView, &WebChatView::linkActivated, this, [](const QString &url) {
        const QUrl parsed(url);
        if (parsed.isValid() &&
            (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https")))
            QDesktopServices::openUrl(parsed);
    });
    connect(m_chatView, &WebChatView::linkHovered, this, [this](const QString &url) {
        if (url.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(url);
    });
    // Clicking a file element in a user message opens the file with the
    // system's default application (non-modal error if that fails).
    connect(m_chatView, &WebChatView::attachmentOpenRequested, this, [this](const QString &path) {
        if (path.isEmpty())
            return;
        QString error;
        if (!AttachmentOpener::open(path, &error)) {
            auto *box = new QMessageBox(QMessageBox::Warning, tr("Could not open attachment"),
                                        error, QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(false);
            box->show();
        }
    });
}

void MainWindow::setupComposerSplitter() {
    connect(m_rightSplitter, &QSplitter::splitterMoved, this, &MainWindow::onComposerSplitterMoved);
    // Attachments grow the composer column (chat area shrinks) while the
    // text input keeps its height.
    connect(m_composer, &Composer::attachmentsChanged, this, [this]() {
        // Defer until the layouts have settled, so the strip height is final.
        QTimer::singleShot(0, this, [this]() { applyComposerHeight(); });
    });
}

void MainWindow::applyComposerHeight() {
    if (!m_rightSplitter || m_rightSplitter->height() <= 0)
        return;
    const int total = m_rightSplitter->height();
    const int strip = m_composer ? m_composer->attachmentStripHeight() : 0;
    const int base = std::clamp(m_uiSettings.composerHeight(), UiSettings::kMinComposerHeight,
                                UiSettings::kMaxComposerHeight);
    const int maxComposer = std::max(UiSettings::kMinComposerHeight, total - kMinChatHeight);
    const int composer = std::clamp(base + strip, UiSettings::kMinComposerHeight, maxComposer);
    m_rightSplitter->setSizes({total - composer, composer});
}

void MainWindow::onComposerSplitterMoved(int /*pos*/, int /*index*/) {
    // Persist the base composer height (the composer column minus the
    // attachment strip, so adding files does not change the stored value).
    if (!m_rightSplitter || !m_composer)
        return;
    const int column = m_rightSplitter->sizes().value(1);
    m_uiSettings.setComposerHeight(column - m_composer->attachmentStripHeight());
}

void MainWindow::createWindowShortcuts() {
    // The sequences come from the shortcut registry so the user can rebind
    // them in the settings; reapplyShortcuts() re-creates the shortcuts when
    // the registry changes. Actions with windowScope=false (e.g. the
    // composer's prompt-template trigger) are bound to their widget instead.
    // The sidebar shortcut shows/hides the chat
    // history, like the classic file-manager panels; the tab shortcuts cycle
    // through the chats in the sidebar's order (newest activity first),
    // wrapping around at the ends.
    auto *newChatShortcut = new QShortcut(m_shortcuts.sequence(QStringLiteral("newChat")), this);
    newChatShortcut->setContext(Qt::WindowShortcut);
    connect(newChatShortcut, &QShortcut::activated, this, &MainWindow::onNewChat);
    m_windowShortcuts.append(newChatShortcut);
    auto *sidebarShortcut =
        new QShortcut(m_shortcuts.sequence(QStringLiteral("toggleSidebar")), this);
    sidebarShortcut->setContext(Qt::WindowShortcut);
    connect(sidebarShortcut, &QShortcut::activated, this,
            [this]() { m_sidebar->setVisible(!m_sidebar->isVisible()); });
    m_windowShortcuts.append(sidebarShortcut);
    auto *nextChatShortcut = new QShortcut(m_shortcuts.sequence(QStringLiteral("nextChat")), this);
    nextChatShortcut->setContext(Qt::WindowShortcut);
    connect(nextChatShortcut, &QShortcut::activated, this, [this]() { cycleChat(1); });
    m_windowShortcuts.append(nextChatShortcut);
    auto *previousChatShortcut =
        new QShortcut(m_shortcuts.sequence(QStringLiteral("previousChat")), this);
    previousChatShortcut->setContext(Qt::WindowShortcut);
    connect(previousChatShortcut, &QShortcut::activated, this, [this]() { cycleChat(-1); });
    m_windowShortcuts.append(previousChatShortcut);
}

void MainWindow::simulateStreamStartForTest() {
    StreamContext *context = ensureContextForTest(m_currentSession);
    context->streaming = true;
    updateStreamingUi();
}

void MainWindow::simulateStreamFinishForTest() {
    if (StreamContext *context = contextFor(m_currentSession))
        finalizeContext(context, tr("Ready"));
}

void MainWindow::reapplyShortcuts() {
    qDeleteAll(m_windowShortcuts);
    m_windowShortcuts.clear();
    createWindowShortcuts();
    const QKeySequence newChatSequence = m_shortcuts.sequence(QStringLiteral("newChat"));
    m_newChatButton->setToolTip(
        tr("Start a new chat (%1)").arg(newChatSequence.toString(QKeySequence::NativeText)));
    if (m_composer) {
        m_composer->setDictateSequence(m_shortcuts.sequence(QStringLiteral("dictate")));
        m_composer->setPromptTriggerSequence(
            m_shortcuts.sequence(QStringLiteral("promptTemplates")));
    }
}

void MainWindow::openSettings(const QString &pageId) {
    SettingsDialog dialog(&m_registry, &m_mcpRegistry, &m_mcpManager, &m_sttRegistry,
                          &m_ttsRegistry, &m_systemPrompt, &m_shortcuts, &m_uiSettings,
                          &m_promptTemplates, this);
    if (!pageId.isEmpty())
        dialog.selectPage(pageId);
    dialog.exec();
}

void MainWindow::loadMcpServersFromDatabase() {
    if (m_database.isOpen())
        m_mcpRegistry.load(m_database);
}

void MainWindow::loadSystemPrompt() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_systemPrompt.load(settings);
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

void MainWindow::loadShortcuts() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_shortcuts.load(settings);
}

void MainWindow::loadPromptTemplates() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_promptTemplates.load(settings);
}

void MainWindow::showPromptTemplateMenu() {
    // Non-modal autocompletion popup above the composer: the composer keeps
    // the keyboard focus, so typing on filters the list live ("/re" shows
    // only "Recherche"); Enter confirms the top entry.
    if (!m_promptMenu) {
        m_promptMenu = new QFrame(this, Qt::Window | Qt::FramelessWindowHint);
        m_promptMenu->setObjectName(QStringLiteral("promptTemplateMenu"));
        m_promptMenu->setStyleSheet(
            QStringLiteral("QFrame#promptTemplateMenu { border: 1px solid palette(mid); "
                           "border-radius: 4px; background: palette(base); }"));
        auto *menuLayout = new QVBoxLayout(m_promptMenu);
        menuLayout->setContentsMargins(2, 2, 2, 2);
        m_promptMenuList = new QListWidget(m_promptMenu);
        m_promptMenuList->setFocusPolicy(Qt::NoFocus);
        menuLayout->addWidget(m_promptMenuList);
        connect(m_promptMenuList, &QListWidget::itemClicked, this,
                [this](QListWidgetItem *item) { confirmPromptMenu(m_promptMenuList->row(item)); });
    }
    m_promptMenuList->clear();
    for (const auto &t : m_promptTemplates.templates()) {
        auto *item = new QListWidgetItem(t.name, m_promptMenuList);
        item->setData(Qt::UserRole, t.id.toString(QUuid::WithoutBraces));
        item->setToolTip(t.text.left(200));
    }
    m_promptMenu->setFixedWidth(320);
    m_promptMenuList->setFixedHeight((std::min(m_promptMenuList->count(), 8) * 24) + 8);
    m_promptMenu->adjustSize();
    const QPoint topLeft = m_composer->mapToGlobal(QPoint(0, 0));
    m_promptMenu->move(topLeft.x(), topLeft.y() - m_promptMenu->height() - 4);
    m_promptMenu->show();
    m_promptMenu->raise();
    onPromptFilterChanged(QString());
    m_composer->focusInput();
}

void MainWindow::onPromptFilterChanged(const QString &filter) {
    if (!m_promptMenu || !m_promptMenu->isVisible())
        return;
    int firstVisible = -1;
    for (int i = 0; i < m_promptMenuList->count(); ++i) {
        auto *item = m_promptMenuList->item(i);
        const bool matches = filter.isEmpty() || item->text().contains(filter, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches && firstVisible == -1)
            firstVisible = i;
    }
    if (firstVisible >= 0)
        m_promptMenuList->setCurrentRow(firstVisible);
}

void MainWindow::confirmPromptMenu(int row) {
    if (!m_promptMenu || !m_promptMenu->isVisible())
        return;
    if (row < 0)
        row = m_promptMenuList->currentRow();
    row = std::max(row, 0); // Enter always confirms the top entry
    auto *item = m_promptMenuList->item(row);
    if (!item || item->isHidden())
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (const PromptTemplate *t = m_promptTemplates.find(id))
        m_composer->applyPromptTemplate(t->text, t->images, t->files);
    closePromptMenu();
}

void MainWindow::closePromptMenu() {
    if (m_promptMenu)
        m_promptMenu->hide();
}

void MainWindow::loadUiSettings() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/ocapi.conf"), QSettings::IniFormat);
    m_uiSettings.load(settings);
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
                    if (m_detectors.contains(backendId) &&
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
        const QString &first = cached.first();
        m_modelPicker->setBackendAndModel(m_modelPicker->backendName(), backendId, first);
        if (m_currentSession)
            m_currentSession->setModel(first);
        saveLastModelForBackend(backendId, first);
    }
}

double MainWindow::throughputFor(StreamContext *context, const QUuid &messageId) {
    if (!contextAlive(context) || messageId != context->streamedMessageId ||
        context->streamedTokens <= 0 || !context->firstToken.isValid())
        return -1.0;
    const qint64 elapsedMs = context->firstToken.msecsTo(QDateTime::currentDateTime());
    if (elapsedMs <= 0)
        return -1.0;
    return context->streamedTokens * 1000.0 / static_cast<double>(elapsedMs);
}

void MainWindow::onSend(const QString &text, const QVector<ImageAttachment> &images,
                        const QVector<FileAttachment> &files) {
    if (!m_currentSession)
        return;
    if (text.isEmpty() && images.isEmpty() && files.isEmpty())
        return;
    if (contextFor(m_currentSession)) {
        // A turn is still running: the composer stays editable while
        // streaming, so queue the follow-up and send it when the current
        // turn finishes.
        m_pendingMessages.append({.text = text, .images = images, .files = files});
        m_composer->clearText();
        m_composer->clearAttachments();
        statusBar()->showMessage(tr("Queued — will be sent when the current response finishes."),
                                 5000);
        return;
    }
    if (!m_editingMessageId.isNull()) {
        // Resending an edited user message: drop it and everything after it
        // from the history (and the database), then send the edited version
        // with the remaining history.
        const QStringList removedIds = m_currentSession->truncateFrom(m_editingMessageId);
        if (m_database.isOpen())
            m_database.deleteMessagesFrom(m_currentSession->id().toString(QUuid::WithoutBraces),
                                          removedIds);
        m_editingMessageId = QUuid();
        m_cancelEditButton->setVisible(false);
    }

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

    StreamContext *context = ensureContext(m_currentSession);
    context->backend = createBackend(*config);
    if (context->backend->supportsTools())
        context->backend->setAvailableTools(m_mcpManager.availableTools());
    context->streamedTokens = 0;
    context->firstToken = QDateTime();
    connectContext(context);
    context->streaming = true;
    updateStreamingUi();

    // Inject the system prompt (with dynamic fields substituted for right
    // now) as the session's first message so every backend picks it up from
    // the conversation. It is prepended without emitting messageAdded, so it
    // is not persisted and not shown in the chat view.
    const QString systemPrompt = m_systemPrompt.substituted();
    if (!systemPrompt.isEmpty()) {
        const auto &msgs = m_currentSession->messages();
        if (!msgs.isEmpty() && msgs.constFirst().role == Role::System) {
            if (msgs.constFirst().content != systemPrompt)
                m_currentSession->updateMessage(msgs.constFirst().id, systemPrompt, false);
        } else {
            Message sys;
            sys.role = Role::System;
            sys.content = systemPrompt;
            m_currentSession->prependMessages({sys});
        }
    }

    const qsizetype count = m_currentSession->messages().size();
    context->backend->sendMessage(m_currentSession, model);
    // The backend appends the assistant message inside sendMessage().
    if (m_currentSession->messages().size() > count) {
        const Message &assistant = m_currentSession->messages().constLast();
        if (assistant.role == Role::Assistant)
            context->streamedMessageId = assistant.id;
    }
}

void MainWindow::startEditMessage(const QString &idString) {
    if (!m_currentSession)
        return;
    const QUuid id(idString);
    const Message *message = nullptr;
    for (const auto &candidate : m_currentSession->messages()) {
        if (candidate.id == id) {
            message = &candidate;
            break;
        }
    }
    if (!message || message->role != Role::User)
        return;
    m_editingMessageId = id;
    m_composer->setText(message->content);
    m_composer->setAttachments(message->images, message->files);
    m_cancelEditButton->setVisible(true);
    m_composer->focusInput();
}

void MainWindow::cancelEditMessage() {
    m_editingMessageId = QUuid();
    m_composer->clearText();
    m_composer->clearAttachments();
    m_cancelEditButton->setVisible(false);
}

void MainWindow::onStop() {
    m_ttsPlayer.stop();
    StreamContext *context = contextFor(m_currentSession);
    if (!context)
        return;
    context->backend->cancel();
    // Do not rely on the backend emitting finished/failed after cancel():
    // end the turn right away, otherwise the sidebar spinner keeps running
    // and the Stop button never turns back into Send. The streamed message
    // keeps its partial content and is marked finished.
    if (!context->streamedMessageId.isNull()) {
        const QString content = messageContent(context, context->streamedMessageId);
        context->session->updateMessage(context->streamedMessageId, content, false);
        context->session->setReasoningStreaming(context->streamedMessageId, false);
    }
    context->runningTools = false;
    finalizeContext(context, tr("Stopped"));
}

void MainWindow::sendPendingIfIdle() {
    if (!m_currentSession || contextFor(m_currentSession) || m_pendingMessages.isEmpty())
        return;
    const PendingMessage next = m_pendingMessages.takeFirst();
    onSend(next.text, next.images, next.files);
}

void MainWindow::connectContext(StreamContext *context) {
    // Route every backend signal to its chat's context: chats run in
    // parallel, so the handlers must know which session a token belongs to.
    connect(context->backend.get(), &IBackend::toolCallsRequested, this,
            [this, context](const QUuid &messageId, const QVector<ToolCall> &toolCalls) {
                onToolCallsRequested(context, messageId, toolCalls);
            });
    connect(context->backend.get(), &IBackend::tokenReceived, this,
            [this, context](const QUuid &messageId, const QString &text) {
                onTokenReceived(context, messageId, text);
            });
    connect(context->backend.get(), &IBackend::reasoningReceived, this,
            [this, context](const QUuid &messageId, const QString &text) {
                onReasoningReceived(context, messageId, text);
            });
    connect(context->backend.get(), &IBackend::finished, this,
            [this, context](const QUuid &messageId) { onStreamFinished(context, messageId); });
    connect(context->backend.get(), &IBackend::failed, this,
            [this, context](const QUuid &messageId, const QString &error) {
                onStreamFailed(context, messageId, error);
            });
}

void MainWindow::onTokenReceived(StreamContext *context, const QUuid &messageId,
                                 const QString &text) {
    if (!contextAlive(context))
        return;
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_SSE"))
        qWarning().noquote() << "[ocapi debug] tokenReceived" << messageId.toString()
                             << "len:" << text.size() << "text:" << text.left(200);
    if (messageId == context->streamedMessageId) {
        ++context->streamedTokens;
        if (!context->firstToken.isValid())
            context->firstToken = QDateTime::currentDateTime();
    }
    context->session->updateMessage(messageId, text, true);
    // The first visible answer token ends the reasoning phase.
    context->session->setReasoningStreaming(messageId, false);
    markChatUnread(context->session);
    // Only the chat the user is watching is spoken aloud.
    if (context->session == m_currentSession && m_ttsRegistry.speakResponses())
        m_ttsPlayer.feedText(text);
    if (m_database.isOpen())
        m_database.updateMessage(messageId.toString(QUuid::WithoutBraces), text, true);
}

void MainWindow::onReasoningReceived(StreamContext *context, const QUuid &messageId,
                                     const QString &text) {
    if (!contextAlive(context))
        return;
    // Reasoning streams before the visible answer; keep the message in the
    // reasoning-streaming state until the answer or the stream end arrives.
    context->session->updateReasoning(messageId, text, true);
    markChatUnread(context->session);
    if (messageId == context->streamedMessageId) {
        ++context->streamedTokens;
        if (!context->firstToken.isValid())
            context->firstToken = QDateTime::currentDateTime();
    }
    if (m_database.isOpen()) {
        // Persist all thinking phases of the message, not just the current one,
        // plus the element order so the chat renders thinking/tool calls in
        // the sequence they happened after a reload.
        QStringList segments;
        QStringList elementOrder;
        for (const auto &message : context->session->messages()) {
            if (message.id == messageId) {
                segments = message.reasoningSegments;
                elementOrder = message.elementOrder;
                break;
            }
        }
        m_database.setReasoning(messageId.toString(QUuid::WithoutBraces), segments);
        m_database.setElementOrder(messageId.toString(QUuid::WithoutBraces), elementOrder);
    }
}

void MainWindow::onStreamFinished(StreamContext *context, const QUuid &messageId) {
    if (!contextAlive(context))
        return;
    const QString content = messageContent(context, messageId);
    if (qEnvironmentVariableIsSet("OCAPI_DEBUG_SSE"))
        qWarning().noquote() << "[ocapi debug] streamFinished" << messageId.toString()
                             << "len:" << content.size() << "content:" << content.left(300);
    context->session->updateMessage(messageId, content, false);
    context->session->setReasoningStreaming(messageId, false);
    markChatUnread(context->session);
    const double tokensPerSecond = throughputFor(context, messageId);
    if (tokensPerSecond > 0.0)
        context->session->setThroughput(messageId, tokensPerSecond);
    if (m_database.isOpen()) {
        m_database.updateMessage(messageId.toString(QUuid::WithoutBraces), content, false);
        if (tokensPerSecond > 0.0)
            m_database.setThroughput(messageId.toString(QUuid::WithoutBraces), tokensPerSecond);
        syncSessionToDatabase(context->session);
    }
    // A tool round is about to continue the turn: the context stays alive
    // and the UI keeps showing the chat as busy (also fixes the double
    // "finished" emission of backends ending a tool-call round).
    if (context->runningTools)
        return;
    finalizeContext(context, tr("Ready"));
}

void MainWindow::onStreamFailed(StreamContext *context, const QUuid &messageId,
                                const QString &error) {
    if (!contextAlive(context))
        return;
    context->session->failMessage(messageId, error);
    markChatUnread(context->session);
    if (context->session == m_currentSession)
        m_ttsPlayer.stop();
    if (m_database.isOpen())
        m_database.failMessage(messageId.toString(QUuid::WithoutBraces), error);
    finalizeContext(context, tr("Error: %1").arg(error));
}

QString MainWindow::messageContent(StreamContext *context, const QUuid &messageId) {
    if (!context)
        return {};
    for (const auto &message : context->session->messages())
        if (message.id == messageId)
            return message.content;
    return {};
}

void MainWindow::finalizeContext(StreamContext *context, const QString &statusMessage) {
    if (!contextAlive(context))
        return;
    context->streaming = false;
    const bool isCurrent = context->session == m_currentSession;
    if (isCurrent) {
        if (m_ttsRegistry.speakResponses())
            m_ttsPlayer.endOfText();
        statusBar()->showMessage(statusMessage);
        m_composer->focusInput();
    }
    updateStreamingUi();
    // The context (and its backend) may still be delivering this very
    // signal; defer the deletion to the next event-loop iteration. Backends
    // can emit failed AND finished for one turn, so this may be scheduled
    // twice; the id-based guard makes the second run a no-op instead of a
    // use-after-free.
    const QUuid sessionId = context->session->id();
    QTimer::singleShot(0, this, [this, sessionId, context]() {
        if (contextAliveFor(sessionId, context))
            destroyContext(context);
    });
}

StreamContext *MainWindow::contextFor(const ChatSession *session) const {
    return session ? m_streams.value(session->id()) : nullptr;
}

StreamContext *MainWindow::ensureContext(ChatSession *session) {
    if (!session)
        return nullptr;
    StreamContext *context = m_streams.value(session->id());
    if (!context) {
        context = new StreamContext;
        context->session = session;
        m_streams.insert(session->id(), context);
    }
    return context;
}

bool MainWindow::contextAlive(const StreamContext *context) const {
    return context && m_streams.value(context->session->id()) == context;
}

bool MainWindow::contextAliveFor(const QUuid &sessionId, const StreamContext *context) const {
    // Callbacks that outlive their context (timers, session signals) must
    // never dereference it: comparing against the map entry detects a
    // deleted context without touching freed memory.
    return context && m_streams.value(sessionId) == context;
}

void MainWindow::destroyContext(StreamContext *context) {
    if (!contextAlive(context))
        return;
    if (m_toolContext == context)
        m_toolContext = nullptr;
    m_streams.remove(context->session->id());
    if (context->backend)
        context->backend->cancel();
    delete context;
    updateStreamingUi();
    // A queued follow-up message can go out now that the turn is over.
    sendPendingIfIdle();
}

void MainWindow::cancelContext(ChatSession *session) {
    if (StreamContext *context = contextFor(session)) {
        context->runningTools = false;
        context->streaming = false;
        if (session == m_currentSession)
            m_ttsPlayer.stop();
        destroyContext(context);
    }
}

void MainWindow::updateStreamingUi() {
    const StreamContext *context = contextFor(m_currentSession);
    // A context whose turn already finished (streaming == false, no tool
    // round running) only waits for its deferred deletion; the chat is not
    // busy anymore, so it must not show "Generating…" — finalizeContext()
    // already put the final status ("Ready"/error) into the status bar.
    const bool busy = context && (context->streaming || context->runningTools);
    m_composer->setBusy(busy);
    m_sendButton->setVisible(!busy);
    m_stopButton->setVisible(busy);
    if (busy)
        statusBar()->showMessage(context->runningTools ? tr("Running MCP tools\u2026")
                                                       : tr("Generating\u2026"));
    m_sidebar->setIndicators(runningSessionIds(), m_unreadSessions);
    updateEmptyState();
}

void MainWindow::markChatUnread(const ChatSession *session) {
    if (!session || session == m_currentSession)
        return;
    if (m_unreadSessions.contains(session->id()))
        return;
    m_unreadSessions.insert(session->id());
    m_sidebar->setIndicators(runningSessionIds(), m_unreadSessions);
}

QSet<QUuid> MainWindow::runningSessionIds() const {
    QSet<QUuid> running;
    for (auto it = m_streams.keyBegin(); it != m_streams.keyEnd(); ++it)
        running.insert(*it);
    return running;
}

void MainWindow::onToolCallsRequested(StreamContext *context, const QUuid &messageId,
                                      const QVector<ToolCall> &toolCalls) {
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
    if (!contextAlive(context) || toolCalls.isEmpty())
        return;
    ChatSession *session = context->session;
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
    session->appendToolCalls(messageId, calls);
    persistToolCalls(session, messageId);
    runToolCalls(context, messageId, calls);
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

bool MainWindow::markRunningToolCallResult(const QString &toolName, const QString &resultText,
                                           bool isError) {
    // Chats run in parallel, so the call can belong to any session; prefer
    // the ones with a running turn, they requested the most recent calls.
    for (auto it = m_streams.constBegin(); it != m_streams.constEnd(); ++it) {
        if (markRunningToolCallResultIn(it.value()->session, toolName, resultText, isError))
            return true;
    }
    return std::ranges::any_of(m_sessions, [&](ChatSession *session) {
        return markRunningToolCallResultIn(session, toolName, resultText, isError);
    });
}

bool MainWindow::markRunningToolCallResultIn(ChatSession *session, const QString &toolName,
                                             const QString &resultText, bool isError) {
    for (const auto &message : session->messages()) {
        for (const auto &call : message.toolCalls) {
            if (call.name == toolName && call.status == ToolCallStatus::Running) {
                const auto status = isError ? ToolCallStatus::Failed : ToolCallStatus::Done;
                session->setToolCallStatus(message.id, call.id, status, resultText);
                if (m_database.isOpen())
                    m_database.updateToolCallStatus(message.id.toString(QUuid::WithoutBraces),
                                                    call.id, status, resultText);
                return true;
            }
        }
    }
    return false;
}

void MainWindow::onToolResultReady(const QString &toolName, const QString &resultText,
                                   bool isError) {
    // Mark the matching tool call finished on the assistant message that
    // requested it; the chat repaints the element without the spinner.
    if (markRunningToolCallResult(toolName, resultText, isError))
        return;
    statusBar()->showMessage(isError ? tr("MCP tool \"%1\" failed.").arg(toolName)
                                     : tr("MCP tool \"%1\" finished.").arg(toolName),
                             5000);
}

void MainWindow::runToolCalls(StreamContext *context, const QUuid &assistantMessageId,
                              const QVector<ToolCall> &toolCalls) {
    Q_UNUSED(assistantMessageId)
    if (!contextAlive(context))
        return;
    ChatSession *session = context->session;
    context->runningTools = true;
    context->pendingToolRounds = static_cast<int>(toolCalls.size());
    updateStreamingUi();
    // Watch for tool result messages; each result decrements the round count.
    const QUuid sessionId = session->id();
    auto *watcher = new QObject(this);
    connect(session, &ChatSession::messageAdded, watcher,
            [this, sessionId, context, watcher](const Message &message) {
                if (message.role != Role::Tool)
                    return;
                if (!contextAliveFor(sessionId, context)) {
                    watcher->deleteLater();
                    return;
                }
                // Keep watching until every call of the round reported back:
                // results of parallel tool calls arrive in later event-loop
                // iterations, so stopping (and deleting the watcher) after the
                // first result left the round count stuck and the conversation
                // without its final assistant answer.
                --context->pendingToolRounds;
                if (context->pendingToolRounds <= 0) {
                    watcher->deleteLater();
                    continueAfterToolResults(context);
                }
            });
    connect(session, &QObject::destroyed, watcher, &QObject::deleteLater);
    for (const auto &call : toolCalls) {
        // executeToolCall returns false when the user must be asked first; in
        // that case the round is accounted for when the permission is decided
        // (see onMcpPermissionRequested). Synchronous failures (unknown tool,
        // disabled tool/server) append a Role::Tool message immediately, which
        // the watcher above picks up.
        m_toolContext = context;
        m_mcpManager.executeToolCall(session, call);
    }
    // When every call failed synchronously, the watcher already fired and the
    // loop continues via continueAfterToolResults. Calls that need a permission
    // decision resume in onMcpPermissionRequested.
}

void MainWindow::continueAfterToolResults(StreamContext *context) {
    // Defer to the next event-loop iteration: this can be reached synchronously
    // from within the backend's toolCallsRequested emission (a synchronously
    // failing tool call or a denied permission appends its tool result
    // immediately, and the watcher in runToolCalls then lands here). Replacing
    // the backend below would destroy the emitting backend while its signal is
    // still being delivered, corrupting the heap.
    const QUuid sessionId = context->session->id();
    QTimer::singleShot(0, this, [this, sessionId, context]() {
        if (contextAliveFor(sessionId, context))
            sendConversationAfterToolResults(context);
    });
}

void MainWindow::sendConversationAfterToolResults(StreamContext *context) {
    if (!contextAliveFor(context->session->id(), context) || !context->runningTools)
        return; // already continued or the turn was cancelled
    ChatSession *session = context->session;
    if (toolDebug::enabled())
        toolDebug::logSession(session, QStringLiteral("session before next round"));
    context->runningTools = false;
    // Send the conversation (now including tool results) back to the model.
    const QString backendId = session->backendId();
    auto *config = m_registry.find(backendId);
    if (!config || !config->enabled) {
        finalizeContext(context, tr("Ready"));
        return;
    }
    QString model = session->model();
    if (model.isEmpty())
        model = m_modelPicker->currentModel().trimmed();
    context->backend = createBackend(*config);
    if (context->backend->supportsTools())
        context->backend->setAvailableTools(m_mcpManager.availableTools());
    context->streamedTokens = 0;
    context->firstToken = QDateTime();
    connectContext(context);
    context->streaming = true;
    updateStreamingUi();
    // Start a new assistant message for this round: the chat then shows each
    // round's thinking, tool calls and answer text as their own message in
    // the order they actually happened, instead of merging the new answer
    // into the message that already ended before the last tool call.
    const qsizetype messageCount = session->messages().size();
    context->backend->sendMessage(session, model);
    if (session->messages().size() > messageCount) {
        const Message &assistant = session->messages().constLast();
        if (assistant.role == Role::Assistant)
            context->streamedMessageId = assistant.id;
    }
}

void MainWindow::onMcpPermissionRequested(const QString &callKey, const QString &serverName,
                                          const QString &toolName, const QJsonObject &arguments) {
    // The request is emitted synchronously from executeToolCall, which stored
    // its context right before; capture it before the modal dialog's nested
    // event loop can run other chats' tool calls.
    StreamContext *context = m_toolContext;
    if (!contextAlive(context))
        return;
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
    const int toolMessagesBefore = [context]() {
        int count = 0;
        for (const auto &message : context->session->messages())
            if (message.role == Role::Tool)
                ++count;
        return count;
    }();
    m_mcpManager.decidePermission(callKey, allow);
    if (!allow && contextAlive(context)) {
        // Denied: a tool message was appended synchronously; account for it.
        int toolMessagesAfter = 0;
        for (const auto &message : context->session->messages())
            if (message.role == Role::Tool)
                ++toolMessagesAfter;
        if (toolMessagesAfter > toolMessagesBefore) {
            --context->pendingToolRounds;
            if (context->pendingToolRounds <= 0)
                continueAfterToolResults(context);
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
