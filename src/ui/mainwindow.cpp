#include "ui/mainwindow.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/chatsession.h"
#include "storage/chatdatabase.h"
#include "ui/backenddialog.h"
#include "ui/chatview.h"
#include "ui/composer.h"
#include "ui/sidebar.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("ocapi"));
    resize(1100, 720);

    if (!openDatabase())
        statusBar()->showMessage(tr("Warning: chat storage unavailable, chats will not be "
                                    "persisted."));
    loadBackendsFromDatabase();

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

    // Toolbar: backend + model selectors, send/stop.
    auto *toolbar = new QWidget(rightPanel);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 6, 8, 6);
    m_backendCombo = new QComboBox(toolbar);
    m_modelCombo = new QComboBox(toolbar);
    m_modelCombo->setEditable(true);
    m_modelCombo->setMinimumWidth(220);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sendButton = new QPushButton(tr("Send"), toolbar);
    m_stopButton = new QPushButton(tr("Stop"), toolbar);
    m_stopButton->setVisible(false);
    toolbarLayout->addWidget(new QLabel(tr("Backend:"), toolbar));
    toolbarLayout->addWidget(m_backendCombo);
    toolbarLayout->addWidget(new QLabel(tr("Model:"), toolbar));
    toolbarLayout->addWidget(m_modelCombo, 1);
    toolbarLayout->addWidget(m_sendButton);
    toolbarLayout->addWidget(m_stopButton);
    rightLayout->addWidget(toolbar);

    m_chatView = new ChatView(rightPanel);
    rightLayout->addWidget(m_chatView, 1);

    m_emptyLabel = new QLabel(tr("Select a chat or start a new one.\n\n"
                                 "Set up a backend via \"Manage backends…\" to get started."),
                              rightPanel);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: gray;"));
    rightLayout->addWidget(m_emptyLabel);
    m_emptyLabel->hide();

    auto *composerRow = new QWidget(rightPanel);
    auto *composerLayout = new QHBoxLayout(composerRow);
    composerLayout->setContentsMargins(8, 0, 8, 8);
    m_composer = new Composer(composerRow);
    m_composer->setMaximumHeight(160);
    composerLayout->addWidget(m_composer, 1);
    rightLayout->addWidget(composerRow);

    splitter->addWidget(m_sidebar);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(1, 1);
    centralLayout->addWidget(splitter);
    setCentralWidget(central);

    m_statusLabel = new QLabel(statusBar());
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->showMessage(tr("Ready"));

    connect(m_sidebar, &Sidebar::newChatRequested, this, &MainWindow::onNewChat);
    connect(m_sidebar, &Sidebar::sessionSelected, this, &MainWindow::onSessionSelected);
    connect(m_sidebar, &Sidebar::sessionDeleteRequested, this,
            &MainWindow::onSessionDeleteRequested);
    connect(m_sidebar, &Sidebar::manageBackendsRequested, this, &MainWindow::onManageBackends);
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

void MainWindow::onManageBackends() {
    BackendDialog dialog(&m_registry, this);
    dialog.exec();
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
    if (!m_currentSession || m_streaming)
        return;
    if (text.isEmpty() && images.isEmpty())
        return;

    const QString backendId = m_currentSession->backendId();
    auto *config = m_registry.find(backendId);
    if (!config) {
        QMessageBox::warning(this, tr("No backend"),
                             tr("Please configure a backend first via \"Manage backends…\"."));
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

void MainWindow::updateEmptyState() {
    const bool noBackends = m_registry.backends().isEmpty();
    const bool noSession = m_currentSession == nullptr;
    m_emptyLabel->setVisible(noBackends || noSession);
    m_chatView->setVisible(!m_emptyLabel->isVisible());
    if (noBackends)
        m_emptyLabel->setText(tr("No backends configured yet.\n\n"
                                 "Click \"Manage backends…\" to add Ollama, llama.cpp, vLLM,\n"
                                 "OpenAI, Anthropic, Google Gemini, Mistral AI or any\n"
                                 "OpenAI-compatible endpoint (local or remote)."));
    else if (noSession)
        m_emptyLabel->setText(tr("Select a chat or start a new one."));
    m_composer->setEnabled(!noSession && !noBackends);
    m_sendButton->setEnabled(!noSession && !noBackends);
}

} // namespace ocapi
