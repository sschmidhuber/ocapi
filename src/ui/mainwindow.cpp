#include "ui/mainwindow.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/chatsession.h"
#include "storage/chatdatabase.h"
#include "ui/chatview.h"
#include "ui/composer.h"
#include "ui/settingsdialog.h"
#include "ui/sidebar.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

namespace ocapi {

namespace {

constexpr int kMessagesPerPage = 50;

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
    message.timestamp = stored.timestamp;
    message.streaming = false;
    message.error = stored.error;
    message.tokensPerSecond = stored.tokensPerSecond;
    message.images = stored.images;
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

    m_chatView = new ChatView(rightPanel);
    rightLayout->addWidget(m_chatView, 1);

    m_emptyLabel =
        new QLabel(tr("Select a chat or start a new one.\n\n"
                      "Set up a backend via \"Tools → Model backends…\" to get started."),
                   rightPanel);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightLayout->addWidget(m_emptyLabel);
    m_emptyLabel->hide();

    auto *composerColumn = new QWidget(rightPanel);
    auto *composerColumnLayout = new QVBoxLayout(composerColumn);
    composerColumnLayout->setContentsMargins(8, 0, 8, 8);
    composerColumnLayout->setSpacing(4);
    m_composer = new Composer(composerColumn);
    m_composer->setMaximumHeight(160);
    composerColumnLayout->addWidget(m_composer);

    // Footer row below the input: backend + model selectors and send/stop.
    auto *footerRow = new QWidget(composerColumn);
    auto *footerLayout = new QHBoxLayout(footerRow);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);
    m_backendCombo = new QComboBox(footerRow);
    m_modelCombo = new QComboBox(footerRow);
    m_modelCombo->setEditable(true);
    m_modelCombo->setMinimumWidth(180);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sendButton = new QPushButton(tr("Send"), footerRow);
    m_stopButton = new QPushButton(tr("Stop"), footerRow);
    m_stopButton->setVisible(false);
    footerLayout->addWidget(m_backendCombo);
    footerLayout->addWidget(m_modelCombo, 1);
    footerLayout->addStretch(1);
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

    m_mcpManager.reload();
    onMcpToolsChanged();

    connect(m_sidebar, &Sidebar::newChatRequested, this, &MainWindow::onNewChat);
    connect(m_sidebar, &Sidebar::sessionSelected, this, &MainWindow::onSessionSelected);
    connect(m_sidebar, &Sidebar::sessionDeleteRequested, this,
            &MainWindow::onSessionDeleteRequested);
    connect(m_sidebar, &Sidebar::settingsRequested, this,
            [this](const QString &pageId) { openSettings(pageId); });
    connect(m_composer, &Composer::sendRequested, this,
            [this](const QString &text, const QVector<ImageAttachment> &images) {
                onSend(text, images);
            });
    connect(m_chatView, &ChatView::olderMessagesRequested, this,
            [this]() { loadOlderMessages(m_currentSession); });
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        if (m_composer->isEmpty())
            return;
        onSend(m_composer->composedText(), m_composer->attachments());
    });
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_currentSession && m_backendCombo->currentData().isValid()) {
            m_currentSession->setBackendId(m_backendCombo->currentData().toString());
            syncSessionToDatabase(m_currentSession);
            disconnectBackend();
        }
        detectModels();
    });
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (m_currentSession) {
            m_currentSession->setModel(text.trimmed());
            syncSessionToDatabase(m_currentSession);
        }
    });
    connect(m_modelCombo, &QComboBox::activated, this,
            [this](int) { onModelSelected(m_modelCombo->currentText()); });
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
    connect(&m_mcpManager, &McpManager::toolResultReady, this,
            [this](const QString &toolName, const QString &resultText, bool isError) {
                Q_UNUSED(resultText)
                statusBar()->showMessage(isError ? tr("MCP tool \"%1\" failed.").arg(toolName)
                                                 : tr("MCP tool \"%1\" finished.").arg(toolName),
                                         5000);
            });
    connect(&m_mcpManager, &McpManager::toolCallFailed, this,
            [this](const QString &toolName, const QString &error) {
                statusBar()->showMessage(tr("MCP tool \"%1\" failed: %2").arg(toolName, error),
                                         10000);
            });

    onBackendChanged();
    refreshSidebar();

    if (m_sessions.isEmpty())
        if (m_sessions.isEmpty())
            createSession();
        else
            onSessionSelected(m_sessions.first()->id());
}

MainWindow::~MainWindow() {
    qDeleteAll(m_sessions);
}

void MainWindow::onNewChat() {
    createSession();
}

void MainWindow::createSession(const QString &backendId, const QString &model) {
    QString effectiveBackendId = backendId;
    if (effectiveBackendId.isEmpty()) {
        const auto enabled = m_registry.enabled();
        if (!enabled.isEmpty())
            effectiveBackendId = enabled.first().id.toString();
    }

    auto *session = new ChatSession(tr("New chat"), effectiveBackendId, this);
    if (!model.isEmpty())
        session->setModel(model);

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

    const bool hasSession = m_currentSession != nullptr;
    m_composer->setEnabled(hasSession);
    m_sendButton->setEnabled(hasSession);
    m_backendCombo->setEnabled(hasSession);
    m_modelCombo->setEnabled(hasSession);
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
    SettingsDialog dialog(&m_registry, &m_mcpRegistry, &m_mcpManager, this);
    if (pageId == QStringLiteral("mcp"))
        dialog.selectPage(QStringLiteral("mcp"));
    dialog.exec();
}

void MainWindow::loadMcpServersFromDatabase() {
    if (m_database.isOpen())
        m_mcpRegistry.load(m_database);
}

bool MainWindow::openDatabase() {
    if (!m_database.open(defaultDatabasePath()))
        return false;
    for (const auto &sessionId : m_database.sessionIds()) {
        const QUuid id(sessionId);
        if (id.isNull())
            continue;
        auto *session = new ChatSession(id, m_database.sessionTitle(sessionId),
                                        m_database.sessionBackendId(sessionId), this);
        session->setModel(m_database.sessionModel(sessionId));
        const int total = m_database.messageCount(sessionId);
        const int initial = qMin(total, kMessagesPerPage);
        for (const auto &stored : m_database.messagesForSession(sessionId, initial, 0))
            appendStoredMessage(session, stored);
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
    const int loaded = session->property("ocapi_loadedCount").toInt();
    if (loaded >= total)
        return;
    m_loadingOlder = true;
    const int pageSize = qMin(kMessagesPerPage, total - loaded);
    QVector<Message> older;
    for (const auto &stored : m_database.messagesForSession(sessionId, pageSize, loaded)) {
        Message message = storedToMessage(stored);
        if (!message.id.isNull())
            older.push_back(message);
    }
    if (!older.isEmpty()) {
        session->prependMessages(older);
        m_chatView->prependMessages(older);
        session->setProperty("ocapi_loadedCount", loaded + static_cast<int>(older.size()));
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
                             message.content, message.timestamp, message.images);
}

void MainWindow::syncSessionToDatabase(ChatSession *session) {
    if (!m_database.isOpen() || !session)
        return;
    m_database.updateSession(*session);
}

void MainWindow::onBackendChanged() {
    m_detectedBackendId.clear();
    refreshBackendSelector();
    updateEmptyState();
    detectModels();
}

void MainWindow::refreshSidebar() {
    m_sidebar->setSessions(m_sessions);
    if (m_currentSession)
        m_sidebar->selectSession(m_currentSession->id());
}

void MainWindow::refreshBackendSelector() {
    const QSignalBlocker blocker(m_backendCombo);
    m_backendCombo->clear();
    for (const auto &config : m_registry.backends()) {
        m_backendCombo->addItem(config.enabled ? config.name : tr("%1 (disabled)").arg(config.name),
                                config.id.toString());
    }

    QString selectedId = m_currentSession ? m_currentSession->backendId() : QString();
    if (selectedId.isEmpty() && !m_registry.backends().isEmpty())
        selectedId = m_registry.backends().first().id.toString();

    int index = m_backendCombo->findData(selectedId);
    if (index < 0 && m_backendCombo->count() > 0)
        index = 0;
    m_backendCombo->setCurrentIndex(index);
    if (m_currentSession && index >= 0)
        m_currentSession->setBackendId(m_backendCombo->currentData().toString());

    // Model combo: default model of the selected backend + editable entry.
    const QSignalBlocker modelBlocker(m_modelCombo);
    m_modelCombo->clear();
    if (m_currentSession && index >= 0) {
        const auto *config = m_registry.find(QUuid(m_backendCombo->currentData().toString()));
        if (config && !config->defaultModel.isEmpty())
            m_modelCombo->addItem(config->defaultModel);
        if (!m_currentSession->model().isEmpty())
            m_modelCombo->setCurrentText(m_currentSession->model());
        else if (config && !config->defaultModel.isEmpty())
            m_currentSession->setModel(config->defaultModel);
    }
}

void MainWindow::onModelSelected(const QString &model) {
    if (m_currentSession)
        m_currentSession->setModel(model.trimmed());
}

void MainWindow::detectModels() {
    const QString backendId = m_backendCombo->currentData().toString();
    const auto *config = m_registry.find(backendId);
    if (!config || !config->enabled)
        return;
    if (m_detectedBackendId == backendId)
        return;
    m_detectedBackendId = backendId;
    m_detectorBackend = createBackend(*config);
    connect(m_detectorBackend.get(), &IBackend::modelsReady, this, &MainWindow::onModelsDetected);
    connect(m_detectorBackend.get(), &IBackend::modelsFailed, this,
            [this](const QString &) { m_detectorBackend.reset(); });
    m_detectorBackend->listModels();
}

void MainWindow::onModelsDetected(const QStringList &models) {
    m_detectorBackend.reset();
    if (models.isEmpty())
        return;
    const QString backendId = m_backendCombo->currentData().toString();
    if (backendId != m_detectedBackendId)
        return;
    const auto *config = m_registry.find(backendId);
    if (!config)
        return;
    // Keep any currently selected model first; add detected models as choices.
    const QString current = m_modelCombo->currentText();
    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();
    if (!config->defaultModel.isEmpty())
        m_modelCombo->addItem(config->defaultModel);
    for (const auto &model : models) {
        if (m_modelCombo->findText(model) < 0)
            m_modelCombo->addItem(model);
    }
    m_modelCombo->setCurrentText(current);
}

double MainWindow::throughputFor(const QUuid &messageId) {
    if (messageId != m_streamedMessageId || m_streamedTokens <= 0 || !m_firstToken.isValid())
        return -1.0;
    const qint64 elapsedMs = m_firstToken.msecsTo(QDateTime::currentDateTime());
    if (elapsedMs <= 0)
        return -1.0;
    return m_streamedTokens * 1000.0 / static_cast<double>(elapsedMs);
}

void MainWindow::onSend(const QString &text, const QVector<ImageAttachment> &images) {
    if (!m_currentSession || m_streaming || m_runningTools)
        return;
    if (text.isEmpty() && images.isEmpty())
        return;

    const QString backendId = m_currentSession->backendId();
    auto *config = m_registry.find(backendId);
    if (!config) {
        QMessageBox::warning(
            this, tr("No backend"),
            tr("Please configure a backend first via \"Tools → Model backends…\"."));
        return;
    }
    if (!config->enabled) {
        QMessageBox::warning(this, tr("Backend disabled"),
                             tr("The backend \"%1\" is disabled.").arg(config->name));
        return;
    }

    QString model = m_currentSession->model();
    if (model.isEmpty())
        model = m_modelCombo->currentText().trimmed();
    if (model.isEmpty()) {
        QMessageBox::warning(this, tr("No model"), tr("Please enter or select a model name."));
        return;
    }
    m_currentSession->setModel(model);

    Message userMessage;
    userMessage.role = Role::User;
    userMessage.content = text;
    userMessage.images = images;
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
        if (assistant.role == Role::Assistant)
            m_streamedMessageId = assistant.id;
    }
}

void MainWindow::onStop() {
    if (m_backend)
        m_backend->cancel();
}

IBackend *MainWindow::activeBackend() {
    return m_backend.get();
}

void MainWindow::connectBackend(IBackend *backend) {
    connect(backend, &IBackend::toolCallsRequested, this, &MainWindow::onToolCallsRequested);
    connect(backend, &IBackend::tokenReceived, this,
            [this](const QUuid &messageId, const QString &text) {
                if (messageId == m_streamedMessageId) {
                    ++m_streamedTokens;
                    if (!m_firstToken.isValid())
                        m_firstToken = QDateTime::currentDateTime();
                }
                if (m_currentSession)
                    m_currentSession->updateMessage(messageId, text, true);
                if (m_database.isOpen())
                    m_database.updateMessage(messageId.toString(QUuid::WithoutBraces), text, true);
            });
    connect(backend, &IBackend::finished, this, [this](const QUuid &messageId) {
        const QString content = messageContent(messageId);
        if (m_currentSession)
            m_currentSession->updateMessage(messageId, content, false);
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
    });
    connect(backend, &IBackend::failed, this, [this](const QUuid &messageId, const QString &error) {
        if (m_currentSession)
            m_currentSession->failMessage(messageId, error);
        if (m_database.isOpen())
            m_database.failMessage(messageId.toString(QUuid::WithoutBraces), error);
        endStreaming(tr("Error: %1").arg(error));
    });
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
    m_composer->setBusy(false);
    m_sendButton->setVisible(true);
    m_stopButton->setVisible(false);
    statusBar()->showMessage(statusMessage);
}

void MainWindow::disconnectBackend() {
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
    if (!m_currentSession || toolCalls.isEmpty())
        return;
    // Record the requested tool calls on the assistant message so the chat UI
    // can render them and the conversation history stays complete.
    m_currentSession->setToolCalls(messageId, toolCalls);
    runToolCalls(m_currentSession, messageId, toolCalls);
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
    if (!session || session != m_currentSession)
        return;
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
        model = m_modelCombo->currentText().trimmed();
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
    const qsizetype count = session->messages().size();
    m_backend->sendMessage(session, model);
    if (session->messages().size() > count) {
        const Message &assistant = session->messages().constLast();
        if (assistant.role == Role::Assistant)
            m_streamedMessageId = assistant.id;
    }
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
                                 "Click \"Tools → Model backends…\" to add Ollama, llama.cpp,\n"
                                 "vLLM, OpenAI, Anthropic, Google Gemini, Mistral AI or any\n"
                                 "OpenAI-compatible endpoint (local or remote)."));
    else if (noSession)
        m_emptyLabel->setText(tr("Select a chat or start a new one."));
    m_composer->setEnabled(!noSession && !noBackends);
    m_sendButton->setEnabled(!noSession && !noBackends);
}

} // namespace ocapi
