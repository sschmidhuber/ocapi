#include "ui/mainwindow.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/chatsession.h"
#include "ui/backenddialog.h"
#include "ui/chatview.h"
#include "ui/composer.h"
#include "ui/sidebar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace ocapi {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("ocapi"));
    resize(1100, 720);

    m_registry.load();

    auto* central = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto* splitter = new QSplitter(central);

    m_sidebar = new Sidebar(splitter);
    m_sidebar->setMinimumWidth(200);
    m_sidebar->setMaximumWidth(320);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Toolbar: backend + model selectors, send/stop.
    auto* toolbar = new QWidget(rightPanel);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
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

    auto* composerRow = new QWidget(rightPanel);
    auto* composerLayout = new QHBoxLayout(composerRow);
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
    connect(m_sidebar, &Sidebar::sessionDeleteRequested, this, &MainWindow::onSessionDeleteRequested);
    connect(m_sidebar, &Sidebar::manageBackendsRequested, this, &MainWindow::onManageBackends);
    connect(m_composer, &Composer::sendRequested, this, &MainWindow::onSend);
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        const QString text = m_composer->toPlainText().trimmed();
        if (!text.isEmpty())
            onSend(text);
    });
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (m_currentSession && m_backendCombo->currentData().isValid()) {
            m_currentSession->setBackendId(m_backendCombo->currentData().toString());
            disconnectBackend();
        }
    });
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (m_currentSession)
            m_currentSession->setModel(text.trimmed());
    });
    connect(m_modelCombo, &QComboBox::activated, this, [this](int) { onModelSelected(m_modelCombo->currentText()); });
    connect(&m_registry, &BackendRegistry::changed, this, &MainWindow::onBackendChanged);

    onBackendChanged();
    refreshSidebar();

    if (m_sessions.isEmpty())
        createSession();
    else
        onSessionSelected(m_sessions.first()->id());
}

MainWindow::~MainWindow()
{
    qDeleteAll(m_sessions);
}

void MainWindow::onNewChat()
{
    createSession();
}

void MainWindow::createSession(const QString& backendId, const QString& model)
{
    QString effectiveBackendId = backendId;
    if (effectiveBackendId.isEmpty()) {
        const auto enabled = m_registry.enabled();
        if (!enabled.isEmpty())
            effectiveBackendId = enabled.first().id.toString();
    }

    auto* session = new ChatSession(tr("New chat"), effectiveBackendId, this);
    if (!model.isEmpty())
        session->setModel(model);

    // Auto-generate a title from the first user message.
    connect(session, &ChatSession::messageAdded, this, [this, session](const Message& message) {
        if (message.role == Role::User && session->title() == tr("New chat")) {
            QString title = message.content;
            title.replace(QLatin1Char('\n'), QLatin1Char(' '));
            if (title.size() > 40)
                title = title.left(40) + QStringLiteral("…");
            session->setTitle(title);
            refreshSidebar();
        }
    });

    m_sessions.push_back(session);
    refreshSidebar();
    onSessionSelected(session->id());
}

void MainWindow::onSessionSelected(const QUuid& id)
{
    disconnectBackend();
    m_currentSession = nullptr;
    for (auto* session : m_sessions) {
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

void MainWindow::onSessionDeleteRequested(const QUuid& id)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if ((*it)->id() == id) {
            if (*it == m_currentSession) {
                disconnectBackend();
                m_currentSession = nullptr;
                m_chatView->setSession(nullptr);
            }
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

void MainWindow::onManageBackends()
{
    BackendDialog dialog(&m_registry, this);
    dialog.exec();
}

void MainWindow::onBackendChanged()
{
    refreshBackendSelector();
    updateEmptyState();
}

void MainWindow::refreshSidebar()
{
    m_sidebar->setSessions(m_sessions);
    if (m_currentSession)
        m_sidebar->selectSession(m_currentSession->id());
}

void MainWindow::refreshBackendSelector()
{
    const QSignalBlocker blocker(m_backendCombo);
    m_backendCombo->clear();
    for (const auto& config : m_registry.backends()) {
        m_backendCombo->addItem(config.enabled ? config.name
                                               : tr("%1 (disabled)").arg(config.name),
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
        const auto* config = m_registry.find(QUuid(m_backendCombo->currentData().toString()));
        if (config && !config->defaultModel.isEmpty())
            m_modelCombo->addItem(config->defaultModel);
        if (!m_currentSession->model().isEmpty())
            m_modelCombo->setCurrentText(m_currentSession->model());
        else if (config && !config->defaultModel.isEmpty())
            m_currentSession->setModel(config->defaultModel);
    }
}

void MainWindow::onModelSelected(const QString& model)
{
    if (m_currentSession)
        m_currentSession->setModel(model.trimmed());
}

void MainWindow::onSend(const QString& text)
{
    if (!m_currentSession || m_streaming)
        return;

    const QString backendId = m_currentSession->backendId();
    auto* config = m_registry.find(backendId);
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
        QMessageBox::warning(this, tr("No model"),
                             tr("Please enter or select a model name."));
        return;
    }
    m_currentSession->setModel(model);

    m_currentSession->appendMessage(Role::User, text);
    m_composer->clear();

    m_backend = createBackend(*config);
    connectBackend(m_backend.get());
    m_streaming = true;
    m_composer->setBusy(true);
    m_sendButton->setVisible(false);
    m_stopButton->setVisible(true);
    statusBar()->showMessage(tr("Generating…"));

    m_backend->sendMessage(m_currentSession, model);
}

void MainWindow::onStop()
{
    if (m_backend)
        m_backend->cancel();
}

IBackend* MainWindow::activeBackend()
{
    return m_backend.get();
}

void MainWindow::connectBackend(IBackend* backend)
{
    connect(backend, &IBackend::tokenReceived, this, [this](const QUuid& messageId, const QString& text) {
        if (m_currentSession)
            m_currentSession->updateMessage(messageId, text, true);
    });
    connect(backend, &IBackend::finished, this, [this](const QUuid& messageId) {
        if (m_currentSession)
            m_currentSession->updateMessage(messageId, m_currentSession->messages().isEmpty()
                                                            ? QString()
                                                            : [&]() {
                                                                  for (const auto& m : m_currentSession->messages())
                                                                      if (m.id == messageId)
                                                                          return m.content;
                                                                  return QString();
                                                              }(),
                                            false);
        m_streaming = false;
        m_composer->setBusy(false);
        m_sendButton->setVisible(true);
        m_stopButton->setVisible(false);
        statusBar()->showMessage(tr("Ready"));
        m_composer->focusInput();
    });
    connect(backend, &IBackend::failed, this, [this](const QUuid& messageId, const QString& error) {
        if (m_currentSession)
            m_currentSession->failMessage(messageId, error);
        m_streaming = false;
        m_composer->setBusy(false);
        m_sendButton->setVisible(true);
        m_stopButton->setVisible(false);
        statusBar()->showMessage(tr("Error: %1").arg(error));
    });
}

void MainWindow::disconnectBackend()
{
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

void MainWindow::updateEmptyState()
{
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
