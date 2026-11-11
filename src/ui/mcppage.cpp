#include "ui/mcppage.h"

#include "backends/mcpclient.h"
#include "core/mcpregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace okapi {

McpPage::McpPage(McpRegistry *registry, McpManager *manager, QWidget *parent)
    : QWidget(parent), m_registry(registry), m_manager(manager) {
    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(120);
    m_list->setMaximumWidth(170);

    auto *rightWidget = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightWidget);

    auto *formLayout = new QFormLayout;
    // Left-aligned labels and fields that fill the whole row, so Name,
    // Transport, URL etc. line up consistently.
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_nameEdit = new QLineEdit(rightWidget);
    m_transportCombo = new QComboBox(rightWidget);
    for (const auto transport : McpServerConfig::allTransports())
        m_transportCombo->addItem(McpServerConfig::transportName(transport),
                                  QVariant::fromValue(static_cast<int>(transport)));

    // Transport-specific rows live in the same form so all labels share one
    // column; updateTransportVisibility() shows only the relevant group.
    m_urlEdit = new QLineEdit(rightWidget);
    m_urlEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:3000/mcp"));
    // Authorization method for HTTP transports: API token (default, sent as
    // Bearer) or custom HTTP headers. Only the selected field is visible;
    // both values are kept so switching modes loses no configuration.
    m_apiKeyRadio = new QRadioButton(tr("API token"), rightWidget);
    m_apiKeyRadio->setObjectName(QStringLiteral("apiKeyRadio"));
    m_apiKeyRadio->setChecked(true);
    m_headersRadio = new QRadioButton(tr("HTTP headers"), rightWidget);
    m_headersRadio->setObjectName(QStringLiteral("headersRadio"));
    m_authModeWidget = new QWidget(rightWidget);
    auto *authModeRow = new QHBoxLayout(m_authModeWidget);
    authModeRow->setContentsMargins(0, 0, 0, 0);
    authModeRow->addWidget(m_apiKeyRadio);
    authModeRow->addWidget(m_headersRadio);
    authModeRow->addStretch();
    m_apiKeyLabel = new QLabel(tr("API token:"), rightWidget);
    m_apiKeyEdit = new QLineEdit(rightWidget);
    m_apiKeyEdit->setObjectName(QStringLiteral("apiKeyEdit"));
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API token"));
    m_headersLabel = new QLabel(tr("HTTP headers:"), rightWidget);
    m_headersEdit = new QLineEdit(rightWidget);
    m_headersEdit->setObjectName(QStringLiteral("headersEdit"));
    m_headersEdit->setPlaceholderText(tr("Authorization: Bearer \u2026 (one per line)"));
    m_httpGroup = {m_urlEdit,    m_authModeWidget, m_apiKeyLabel,
                   m_apiKeyEdit, m_headersLabel,   m_headersEdit};
    m_commandEdit = new QLineEdit(rightWidget);
    m_commandEdit->setPlaceholderText(QStringLiteral("npx"));
    m_argumentsEdit = new QLineEdit(rightWidget);
    m_argumentsEdit->setPlaceholderText(tr("-y @modelcontextprotocol/server-filesystem /tmp"));
    m_environmentEdit = new QLineEdit(rightWidget);
    m_environmentEdit->setPlaceholderText(tr("KEY=VALUE (one per line)"));
    m_stdioGroup = {m_commandEdit, m_argumentsEdit, m_environmentEdit};

    m_defaultPermissionCombo = new QComboBox(rightWidget);
    for (const auto permission : McpServerConfig::allPermissions())
        m_defaultPermissionCombo->addItem(McpServerConfig::permissionName(permission),
                                          QVariant::fromValue(static_cast<int>(permission)));
    m_enabledBox = new QCheckBox(tr("Enabled"), rightWidget);
    m_enabledBox->setChecked(true);
    m_testButton = new QPushButton(tr("Test connection"), rightWidget);
    m_testResultLabel = new QLabel(rightWidget);
    m_testResultLabel->setWordWrap(true);
    m_serverInfoLabel = new QLabel(rightWidget);
    m_serverInfoLabel->setWordWrap(true);
    m_serverInfoLabel->setStyleSheet(QStringLiteral("color: gray;"));

    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Transport:"), m_transportCombo);
    formLayout->addRow(tr("URL:"), m_urlEdit);
    formLayout->addRow(tr("Authorization:"), m_authModeWidget);
    formLayout->addRow(m_apiKeyLabel, m_apiKeyEdit);
    formLayout->addRow(m_headersLabel, m_headersEdit);
    formLayout->addRow(tr("Command:"), m_commandEdit);
    formLayout->addRow(tr("Arguments:"), m_argumentsEdit);
    formLayout->addRow(tr("Environment:"), m_environmentEdit);
    formLayout->addRow(tr("Default permission:"), m_defaultPermissionCombo);
    formLayout->addRow(m_enabledBox);
    formLayout->addRow(m_testButton);
    formLayout->addRow(m_testResultLabel);
    formLayout->addRow(m_serverInfoLabel);
    m_testResultLabel->hide();
    m_serverInfoLabel->hide();
    rightLayout->addLayout(formLayout);

    auto *toolsLabel = new QLabel(tr("Tools and permissions:"), rightWidget);
    rightLayout->addWidget(toolsLabel);
    m_toolsTable = new QTableWidget(0, 3, rightWidget);
    m_toolsTable->setHorizontalHeaderLabels({tr("Tool"), tr("Description"), tr("Permission")});
    m_toolsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_toolsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_toolsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_toolsTable->setSelectionMode(QAbstractItemView::NoSelection);
    rightLayout->addWidget(m_toolsTable, 1);

    auto *listButtons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("Add"), splitter);
    auto *removeButton = new QPushButton(tr("Remove"), splitter);
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    auto *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(m_list);
    leftLayout->addLayout(listButtons);
    auto *leftWidget = new QWidget(splitter);
    leftWidget->setLayout(leftLayout);
    // Cap the container as well: otherwise the splitter can allocate it more
    // width than the server list (which is capped), leaving an empty gap
    // between the list and the form until the handle is moved once.
    leftWidget->setMaximumWidth(m_list->maximumWidth() + 20);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    // Stretch factor 0 for the server list: extra width always goes to the
    // form, never to the list (same first-show sizing issue as the settings
    // sidebar).
    splitter->setStretchFactor(0, 1);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(splitter);

    connect(addButton, &QPushButton::clicked, this, &McpPage::onAddServer);
    connect(removeButton, &QPushButton::clicked, this, &McpPage::onRemoveServer);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_transportCombo, &QComboBox::currentIndexChanged, this, &McpPage::onTransportChanged);
    connect(m_testButton, &QPushButton::clicked, this, &McpPage::onTestConnection);
    for (auto *edit : {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_commandEdit, m_argumentsEdit,
                       m_environmentEdit, m_headersEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &McpPage::onFieldEdited);
    }
    connect(m_defaultPermissionCombo, &QComboBox::currentIndexChanged, this,
            &McpPage::onFieldEdited);
    // Switching the authorization mode only toggles visibility; the entered
    // values of both fields are kept.
    connect(m_apiKeyRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked || m_updatingForm)
            return;
        m_apiKeyLabel->setVisible(true);
        m_apiKeyEdit->setVisible(true);
        m_headersLabel->setVisible(false);
        m_headersEdit->setVisible(false);
    });
    connect(m_headersRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked || m_updatingForm)
            return;
        m_apiKeyLabel->setVisible(false);
        m_apiKeyEdit->setVisible(false);
        m_headersLabel->setVisible(true);
        m_headersEdit->setVisible(true);
    });
    connect(m_enabledBox, &QCheckBox::toggled, this, &McpPage::onFieldEdited);
    connect(m_toolsTable, &QTableWidget::cellChanged, this, &McpPage::onPermissionChanged);

    populateList();
    if (!m_registry->servers().isEmpty())
        m_list->setCurrentRow(0);
    else
        onSelectionChanged();
}

void McpPage::populateList() {
    m_list->clear();
    for (const auto &config : m_registry->servers()) {
        auto *item = new QListWidgetItem(
            config.enabled ? config.name : tr("%1 (disabled)").arg(config.name), m_list);
        item->setData(Qt::UserRole, config.id.toString());
    }
}

void McpPage::onAddServer() {
    auto &config = m_registry->add(tr("New MCP server"), McpTransport::StreamableHttp);
    m_registry->save();
    populateList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (QUuid(m_list->item(i)->data(Qt::UserRole).toString()) == config.id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void McpPage::onRemoveServer() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (QMessageBox::question(this, tr("Remove MCP server"),
                              tr("Remove the MCP server \"%1\"?").arg(item->text())) !=
        QMessageBox::Yes)
        return;
    m_registry->remove(id);
    m_registry->save();
    populateList();
    onSelectionChanged();
}

void McpPage::onSelectionChanged() {
    m_updatingForm = true;
    auto *item = m_list->currentItem();
    const bool hasSelection = item != nullptr;
    const QList<QWidget *> widgets{m_nameEdit,       m_transportCombo, m_urlEdit,
                                   m_authModeWidget, m_apiKeyEdit,     m_headersEdit,
                                   m_commandEdit,    m_argumentsEdit,  m_environmentEdit,
                                   m_enabledBox,     m_testButton,     m_toolsTable};
    for (auto *widget : widgets)
        widget->setEnabled(hasSelection);
    m_defaultPermissionCombo->setEnabled(hasSelection);
    m_testResultLabel->clear();
    m_testResultLabel->hide();
    m_serverInfoLabel->clear();
    m_serverInfoLabel->hide();
    m_toolsTable->setRowCount(0);
    if (hasSelection) {
        if (const auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()))) {
            loadConfigIntoForm(*config);
            refreshToolPermissions(*config);
        }
    } else {
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_apiKeyEdit->clear();
        m_commandEdit->clear();
        m_argumentsEdit->clear();
        m_environmentEdit->clear();
        m_headersEdit->clear();
        m_enabledBox->setChecked(true);
    }
    updateTransportVisibility();
    m_updatingForm = false;
}

void McpPage::onTransportChanged(int index) {
    if (m_updatingForm)
        return;
    updateTransportVisibility();
    onFieldEdited();
}

void McpPage::updateTransportVisibility() {
    const auto transport = static_cast<McpTransport>(m_transportCombo->currentData().toInt());
    const bool stdio = transport == McpTransport::Stdio;
    for (auto *w : m_stdioGroup)
        w->setVisible(stdio);
    for (auto *w : m_httpGroup)
        w->setVisible(!stdio);
    if (!stdio) {
        // Keep the auth-mode selection visible state consistent.
        const bool useApiKey = m_apiKeyRadio->isChecked();
        m_apiKeyLabel->setVisible(useApiKey);
        m_apiKeyEdit->setVisible(useApiKey);
        m_headersLabel->setVisible(!useApiKey);
        m_headersEdit->setVisible(!useApiKey);
    }
}

void McpPage::onFieldEdited() {
    if (m_updatingForm)
        return;
    auto *config = currentConfig();
    if (!config)
        return;
    saveFormIntoConfig(*config);
    if (auto *item = m_list->currentItem())
        item->setText(config->enabled ? config->name : tr("%1 (disabled)").arg(config->name));
    m_registry->save();
    emit m_registry->changed();
}

McpServerConfig *McpPage::currentConfig() {
    auto *item = m_list->currentItem();
    if (!item)
        return nullptr;
    return m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
}

void McpPage::onTestConnection() {
    auto *config = currentConfig();
    if (!config)
        return;
    saveFormIntoConfig(*config);
    m_registry->save();

    delete m_testClient;
    m_testTools.clear();
    m_testClient = new McpClient(*config, this);
    connect(m_testClient, &McpClient::connected, this, &McpPage::onTestConnected);
    connect(m_testClient, &McpClient::connectionFailed, this, &McpPage::onTestFailed);
    connect(m_testClient, &McpClient::toolsReady, this, &McpPage::onTestToolsReady);

    m_testButton->setEnabled(false);
    m_testResultLabel->setText(tr("Testing connection…"));
    m_testResultLabel->show();
    m_serverInfoLabel->clear();
    m_serverInfoLabel->hide();
    m_testClient->connectToServer();
}

void McpPage::onTestConnected(const McpServerInfo &info) {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Connection successful."));
    m_testResultLabel->show();
    refreshCapabilities(info, m_testTools);
}

void McpPage::onTestFailed(const QString &error) {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Connection failed: %1").arg(error));
    m_testResultLabel->show();
    if (m_testClient) {
        m_testClient->deleteLater();
        m_testClient = nullptr;
    }
}

void McpPage::onTestToolsReady(const QVector<McpTool> &tools) {
    m_testTools = tools;
    McpServerInfo info;
    if (m_testClient)
        info = m_testClient->serverInfo();
    refreshCapabilities(info, m_testTools);
    // Seed the permission table with the discovered tools so the user can
    // set per-tool permissions right away.
    if (auto *config = currentConfig()) {
        refreshToolPermissions(*config);
    }
}

void McpPage::refreshCapabilities(const McpServerInfo &info, const QVector<McpTool> &tools) {
    QStringList lines;
    if (!info.name.isEmpty())
        lines.append(tr("Server: %1 %2").arg(info.name, info.version));
    QStringList caps;
    if (info.capabilities.tools)
        caps.append(tr("tools"));
    if (info.capabilities.resources)
        caps.append(tr("resources"));
    if (info.capabilities.prompts)
        caps.append(tr("prompts"));
    if (info.capabilities.logging)
        caps.append(tr("logging"));
    if (!caps.isEmpty())
        lines.append(tr("Capabilities: %1").arg(caps.join(QStringLiteral(", "))));
    lines.append(tr("%n tool(s) discovered.", nullptr, static_cast<int>(tools.size())));
    m_serverInfoLabel->setText(lines.join(QStringLiteral("\n")));
    m_serverInfoLabel->show();
}

void McpPage::refreshToolPermissions(const McpServerConfig &config) {
    const QSignalBlocker blocker(m_toolsTable);
    // Prefer the tools discovered by the manager (live connection); fall back
    // to the tools found during the last connection test.
    QVector<McpTool> tools = m_testTools;
    if (m_manager) {
        const auto all = m_manager->allTools();
        tools.clear();
        for (const auto &tool : all) {
            if (tool.serverId == config.id.toString(QUuid::WithoutBraces))
                tools.push_back(tool);
        }
        if (tools.isEmpty())
            tools = m_testTools;
    }
    m_toolsTable->setRowCount(static_cast<int>(tools.size()));
    for (int row = 0; row < tools.size(); ++row) {
        const auto &tool = tools.at(row);
        // Strip the "server__" prefix for display.
        QString displayName = tool.name;
        const QString prefix = config.name + QStringLiteral("__");
        if (displayName.startsWith(prefix))
            displayName = displayName.mid(prefix.size());

        auto *nameItem = new QTableWidgetItem(displayName);
        nameItem->setFlags(Qt::ItemIsEnabled);
        nameItem->setToolTip(tool.name);
        m_toolsTable->setItem(row, 0, nameItem);

        auto *descriptionItem = new QTableWidgetItem(tool.description);
        descriptionItem->setFlags(Qt::ItemIsEnabled);
        m_toolsTable->setItem(row, 1, descriptionItem);

        auto *permissionCombo = new QComboBox(m_toolsTable);
        for (const auto permission : McpServerConfig::allPermissions()) {
            permissionCombo->addItem(McpServerConfig::permissionName(permission),
                                     QVariant::fromValue(static_cast<int>(permission)));
        }
        const McpPermission effective = config.permissionForTool(displayName);
        const int index =
            permissionCombo->findData(QVariant::fromValue(static_cast<int>(effective)));
        permissionCombo->setCurrentIndex(index < 0 ? 1 : index);
        m_toolsTable->setCellWidget(row, 2, permissionCombo);
    }
}

void McpPage::onPermissionChanged(int row, int column) {
    Q_UNUSED(column)
    if (m_updatingForm || row < 0 || row >= m_toolsTable->rowCount())
        return;
    auto *config = currentConfig();
    auto *nameItem = m_toolsTable->item(row, 0);
    auto *combo = qobject_cast<QComboBox *>(m_toolsTable->cellWidget(row, 2));
    if (!config || !nameItem || !combo)
        return;
    const auto permission = static_cast<McpPermission>(combo->currentData().toInt());
    // Persist under the un-namespaced tool name.
    m_registry->setPermissionForTool(config->id, nameItem->text(), permission);
    m_registry->save();
}

void McpPage::loadConfigIntoForm(const McpServerConfig &config) {
    m_nameEdit->setText(config.name);
    const int transportIndex =
        m_transportCombo->findData(QVariant::fromValue(static_cast<int>(config.transport)));
    m_transportCombo->setCurrentIndex(transportIndex < 0 ? 0 : transportIndex);
    m_urlEdit->setText(config.url);
    m_apiKeyEdit->setText(config.apiKey);
    m_commandEdit->setText(config.command);
    // API key mode when a key is set or no custom headers exist (default).
    const bool useApiKey = !config.apiKey.isEmpty() || config.headers.isEmpty();
    m_apiKeyRadio->setChecked(useApiKey);
    m_headersRadio->setChecked(!useApiKey);
    m_apiKeyLabel->setVisible(useApiKey);
    m_apiKeyEdit->setVisible(useApiKey);
    m_headersLabel->setVisible(!useApiKey);
    m_headersEdit->setVisible(!useApiKey);
    m_argumentsEdit->setText(config.arguments.join(QLatin1Char(' ')));
    m_environmentEdit->setText(config.environment.join(QLatin1Char('\n')));
    m_headersEdit->setText(config.headers.join(QLatin1Char('\n')));
    const int permissionIndex = m_defaultPermissionCombo->findData(
        QVariant::fromValue(static_cast<int>(config.defaultPermission)));
    m_defaultPermissionCombo->setCurrentIndex(permissionIndex < 0 ? 1 : permissionIndex);
    m_enabledBox->setChecked(config.enabled);
    updateTransportVisibility();
}

void McpPage::saveFormIntoConfig(McpServerConfig &config) {
    config.name =
        m_nameEdit->text().trimmed().isEmpty() ? tr("MCP server") : m_nameEdit->text().trimmed();
    config.transport = static_cast<McpTransport>(m_transportCombo->currentData().toInt());
    config.url = m_urlEdit->text().trimmed();
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.command = m_commandEdit->text().trimmed();
    config.arguments = m_argumentsEdit->text().trimmed().isEmpty()
                           ? QStringList{}
                           : m_argumentsEdit->text().trimmed().split(QLatin1Char(' '));
    const auto splitLines = [](const QString &text) {
        QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (auto &line : lines)
            line = line.trimmed();
        lines.removeAll(QString());
        return lines;
    };
    config.environment = splitLines(m_environmentEdit->text());
    config.headers = splitLines(m_headersEdit->text());
    config.defaultPermission =
        static_cast<McpPermission>(m_defaultPermissionCombo->currentData().toInt());
    config.enabled = m_enabledBox->isChecked();
}

} // namespace okapi
