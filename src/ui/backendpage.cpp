#include "ui/backendpage.h"

#include "backends/backendfactory.h"
#include "backends/ibackend.h"
#include "core/backendregistry.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace ocapi {

BackendPage::BackendPage(BackendRegistry *registry, QWidget *parent)
    : QWidget(parent), m_registry(registry) {
    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);

    auto *formWidget = new QWidget(splitter);
    auto *formLayout = new QFormLayout(formWidget);
    m_nameEdit = new QLineEdit(formWidget);
    m_kindCombo = new QComboBox(formWidget);
    for (auto kind : BackendConfig::allKinds()) {
        m_kindCombo->addItem(BackendConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_urlEdit = new QLineEdit(formWidget);
    m_urlEdit->setPlaceholderText(tr("http://127.0.0.1:11434"));
    m_apiKeyEdit = new QLineEdit(formWidget);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API key"));
    m_modelEdit = new QLineEdit(formWidget);
    m_modelEdit->setPlaceholderText(tr("Default model (optional)"));
    m_enabledBox = new QCheckBox(tr("Enabled"), formWidget);
    m_enabledBox->setChecked(true);
    m_testButton = new QPushButton(tr("Test connection"), formWidget);
    m_testResultLabel = new QLabel(formWidget);
    m_testResultLabel->setWordWrap(true);

    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Type:"), m_kindCombo);
    formLayout->addRow(tr("Base URL:"), m_urlEdit);
    formLayout->addRow(tr("API key:"), m_apiKeyEdit);
    formLayout->addRow(tr("Default model:"), m_modelEdit);
    formLayout->addRow(m_enabledBox);
    formLayout->addRow(m_testButton);
    formLayout->addRow(m_testResultLabel);
    m_testResultLabel->hide();

    auto *listButtons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("Add"));
    auto *removeButton = new QPushButton(tr("Remove"));
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    auto *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(m_list);
    leftLayout->addLayout(listButtons);
    auto *leftWidget = new QWidget(splitter);
    leftWidget->setLayout(leftLayout);

    splitter->addWidget(leftWidget);
    splitter->addWidget(formWidget);
    splitter->setStretchFactor(1, 1);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(splitter);

    connect(addButton, &QPushButton::clicked, this, &BackendPage::onAddBackend);
    connect(removeButton, &QPushButton::clicked, this, &BackendPage::onRemoveBackend);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, &BackendPage::onKindChanged);
    connect(m_testButton, &QPushButton::clicked, this, &BackendPage::onTestConnection);
    for (auto *edit : {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_modelEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &BackendPage::onFieldEdited);
    }
    connect(m_enabledBox, &QCheckBox::toggled, this, &BackendPage::onFieldEdited);

    populateList();
    if (!m_registry->backends().isEmpty())
        m_list->setCurrentRow(0);
    else
        onSelectionChanged();
}

void BackendPage::populateList() {
    m_list->clear();
    for (const auto &config : m_registry->backends()) {
        auto *item = new QListWidgetItem(
            config.enabled ? config.name : tr("%1 (disabled)").arg(config.name), m_list);
        item->setData(Qt::UserRole, config.id.toString());
    }
}

void BackendPage::onAddBackend() {
    auto &config = m_registry->add(tr("New backend"), BackendKind::Ollama);
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

void BackendPage::onRemoveBackend() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (QMessageBox::question(this, tr("Remove backend"),
                              tr("Remove the backend \"%1\"?").arg(item->text())) !=
        QMessageBox::Yes)
        return;
    m_registry->remove(id);
    m_registry->save();
    populateList();
    onSelectionChanged();
}

void BackendPage::onSelectionChanged() {
    m_updatingForm = true;
    auto *item = m_list->currentItem();
    const bool hasSelection = item != nullptr;
    m_nameEdit->setEnabled(hasSelection);
    m_kindCombo->setEnabled(hasSelection);
    m_urlEdit->setEnabled(hasSelection);
    m_apiKeyEdit->setEnabled(hasSelection);
    m_modelEdit->setEnabled(hasSelection);
    m_enabledBox->setEnabled(hasSelection);
    m_testButton->setEnabled(hasSelection);
    m_testResultLabel->clear();
    m_testResultLabel->hide();
    if (hasSelection) {
        const auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
        if (config)
            loadConfigIntoForm(*config);
    } else {
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_apiKeyEdit->clear();
        m_modelEdit->clear();
        m_enabledBox->setChecked(true);
    }
    updateUrlVisibility();
    m_updatingForm = false;
}

void BackendPage::onKindChanged(int index) {
    if (m_updatingForm || !m_urlEdit->isEnabled())
        return;
    const auto kind = static_cast<BackendKind>(m_kindCombo->itemData(index).toInt());
    // Only fill in the default URL when the field is empty or still holds
    // another provider's default, to avoid clobbering user input.
    if (m_urlEdit->text().isEmpty())
        m_urlEdit->setText(BackendConfig::defaultBaseUrl(kind));
    updateUrlVisibility();
    onFieldEdited();
}

void BackendPage::onFieldEdited() {
    if (m_updatingForm)
        return;
    auto *item = m_list->currentItem();
    if (!item)
        return;
    auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;
    saveFormIntoConfig(*config);
    item->setText(config->enabled ? config->name : tr("%1 (disabled)").arg(config->name));
    m_registry->save();
    emit m_registry->changed();
}

void BackendPage::onTestConnection() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;

    saveFormIntoConfig(*config);
    m_registry->save();

    if (BackendConfig::requiresApiKey(config->kind) && config->apiKey.isEmpty()) {
        m_testResultLabel->setText(tr("Please enter an API key first."));
        m_testResultLabel->show();
        return;
    }

    delete m_testBackend;
    m_testBackend = createBackend(*config).release();
    m_testBackend->setParent(this);
    connect(m_testBackend, &IBackend::modelsReady, this, &BackendPage::onTestModelsReady);
    connect(m_testBackend, &IBackend::modelsFailed, this, &BackendPage::onTestModelsFailed);

    m_testButton->setEnabled(false);
    m_testResultLabel->setText(tr("Testing connection…"));
    m_testResultLabel->show();
    m_testBackend->listModels();
}

void BackendPage::onTestModelsReady(const QStringList &models) {
    m_testButton->setEnabled(true);
    if (models.isEmpty()) {
        m_testResultLabel->setText(tr("Connection successful, but no models were reported."));
    } else {
        QString text = tr("Connection successful. %n model(s) detected:", nullptr,
                          static_cast<int>(models.size()));
        const int total = static_cast<int>(models.size());
        const int shown = std::min(total, 8);
        for (int i = 0; i < shown; ++i)
            text += QStringLiteral("\n• %1").arg(models.at(i));
        if (total > shown)
            text += QStringLiteral("\n• …");
        if (m_modelEdit->text().trimmed().isEmpty() && !models.isEmpty()) {
            m_modelEdit->setText(models.first());
            onFieldEdited();
        }
    }
}

void BackendPage::onTestModelsFailed(const QString &error) {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Connection failed: %1").arg(error));
}

void BackendPage::loadConfigIntoForm(const BackendConfig &config) {
    m_nameEdit->setText(config.name);
    const int kindIndex = m_kindCombo->findData(QVariant::fromValue(static_cast<int>(config.kind)));
    m_kindCombo->setCurrentIndex(kindIndex < 0 ? 0 : kindIndex);
    m_urlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(config.apiKey);
    m_modelEdit->setText(config.defaultModel);
    m_enabledBox->setChecked(config.enabled);
}

void BackendPage::saveFormIntoConfig(BackendConfig &config) {
    config.name = m_nameEdit->text().trimmed().isEmpty() ? BackendConfig::kindName(config.kind)
                                                         : m_nameEdit->text().trimmed();
    config.kind = currentKind();
    config.baseUrl = m_urlEdit->text().trimmed();
    // Local servers need an explicit URL; cloud providers use their default.
    if (config.baseUrl.isEmpty() && !BackendConfig::isCloud(config.kind))
        config.baseUrl = BackendConfig::defaultBaseUrl(config.kind);
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.defaultModel = m_modelEdit->text().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

void BackendPage::updateUrlVisibility() {
    const bool cloud = BackendConfig::isCloud(currentKind());
    // For cloud providers the base URL is optional (provider default is used);
    // keep the row visible only when the user set a custom endpoint.
    const bool visible = !cloud || !m_urlEdit->text().trimmed().isEmpty();
    m_urlEdit->setVisible(visible);
    m_urlEdit->setPlaceholderText(cloud ? tr("Custom endpoint (optional)")
                                        : QStringLiteral("http://127.0.0.1:11434"));
    if (auto *form = qobject_cast<QFormLayout *>(m_urlEdit->parentWidget()->layout()))
        if (auto *label = form->labelForField(m_urlEdit))
            label->setVisible(visible);
}

BackendKind BackendPage::currentKind() const {
    return static_cast<BackendKind>(m_kindCombo->currentData().toInt());
}

} // namespace ocapi
