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
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace ocapi {

namespace {

constexpr int TestTimeoutMs = 15000;

} // namespace

BackendPage::BackendPage(BackendRegistry *registry, QWidget *parent)
    : QWidget(parent), m_registry(registry) {
    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);

    auto *formWidget = new QWidget(splitter);
    auto *formLayout = new QFormLayout(formWidget);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_kindCombo = new QComboBox(formWidget);
    for (auto kind : BackendConfig::allKinds()) {
        m_kindCombo->addItem(BackendConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_nameEdit = new QLineEdit(formWidget);
    m_urlEdit = new QLineEdit(formWidget);
    m_urlEdit->setAccessibleName(tr("Base URL"));
    m_apiKeyEdit = new QLineEdit(formWidget);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API key"));
    m_modelCombo = new QComboBox(formWidget);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    // Match the width of the line edits above instead of shrinking to the
    // current entry; the layout policy makes it grow with the dialog.
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modelCombo->setMinimumContentsLength(24);
    m_enabledBox = new QCheckBox(tr("Enabled"), formWidget);
    m_enabledBox->setChecked(true);
    m_testButton = new QPushButton(tr("Test connection"), formWidget);
    m_testResultLabel = new QLabel(formWidget);
    m_testResultLabel->setWordWrap(true);
    m_testResultLabel->hide();
    m_testTimer = new QTimer(this);
    m_testTimer->setSingleShot(true);

    formLayout->addRow(tr("Provider:"), m_kindCombo);
    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Base URL:"), m_urlEdit);
    formLayout->addRow(tr("API key:"), m_apiKeyEdit);
    formLayout->addRow(tr("Default model:"), m_modelCombo);
    formLayout->addRow(m_enabledBox);
    formLayout->addRow(m_testButton);
    formLayout->addRow(m_testResultLabel);

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
    connect(m_testTimer, &QTimer::timeout, this, &BackendPage::onTestTimeout);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &BackendPage::onFieldEdited);
    for (auto *edit : {m_urlEdit, m_apiKeyEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &BackendPage::onFieldEdited);
    }
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_updatingForm)
            onFieldEdited();
    });
    connect(m_modelCombo->lineEdit(), &QLineEdit::textEdited, this, &BackendPage::onFieldEdited);
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
    const auto kind = BackendKind::Ollama;
    auto &config = m_registry->add(BackendConfig::kindName(kind), kind);
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
    m_hasSelection = hasSelection;
    m_kindCombo->setEnabled(hasSelection);
    m_nameEdit->setEnabled(hasSelection);
    m_urlEdit->setEnabled(hasSelection);
    m_apiKeyEdit->setEnabled(hasSelection);
    m_modelCombo->setEnabled(hasSelection);
    m_enabledBox->setEnabled(hasSelection);
    m_testButton->setEnabled(hasSelection);
    m_testResultLabel->clear();
    m_testResultLabel->hide();
    m_testTimer->stop();
    if (hasSelection) {
        const auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
        if (config)
            loadConfigIntoForm(*config);
    } else {
        m_kindCombo->setCurrentIndex(0);
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_apiKeyEdit->clear();
        m_modelCombo->clear();
        m_enabledBox->setChecked(true);
    }
    updateUrlState();
    m_updatingForm = false;
}

void BackendPage::onKindChanged(int index) {
    if (m_updatingForm || !m_kindCombo->isEnabled())
        return;
    const auto kind = static_cast<BackendKind>(m_kindCombo->itemData(index).toInt());
    // Follow the provider name while the user has not chosen a custom name.
    if (nameFollowsProvider(m_nameEdit->text()))
        m_nameEdit->setText(BackendConfig::kindName(kind));
    // Self-hosted servers get the provider's default URL prefilled; cloud
    // providers use their built-in endpoint, so drop any previously entered
    // self-hosted URL instead of sending it to the cloud API.
    const bool cloud = BackendConfig::isCloud(kind);
    if (cloud) {
        m_urlEdit->clear();
    } else if (m_urlEdit->text().isEmpty()) {
        m_urlEdit->setText(BackendConfig::defaultBaseUrl(kind));
    }
    updateUrlState();
    onFieldEdited();
}

void BackendPage::onFieldEdited() {
    if (m_updatingForm || !m_hasSelection)
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
    m_testTimer->start(TestTimeoutMs);
    m_testBackend->listModels();
}

void BackendPage::onTestTimeout() {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(
        tr("Connection failed: no response within %1 seconds.").arg(TestTimeoutMs / 1000));
    delete m_testBackend;
    m_testBackend = nullptr;
}

void BackendPage::onTestModelsReady(const QStringList &models) {
    m_testTimer->stop();
    m_testButton->setEnabled(true);
    if (models.isEmpty()) {
        m_testResultLabel->setText(tr("Connection successful, but no models were reported."));
        return;
    }
    m_testResultLabel->setText(tr("Connection successful. %n model(s) detected.", nullptr,
                                  static_cast<int>(models.size())));
    // Offer the fetched models in the default-model combo. The combo stays
    // editable so a custom model name can be typed when the backend does
    // not report its models.
    auto *item = m_list->currentItem();
    auto *config = item ? m_registry->find(QUuid(item->data(Qt::UserRole).toString())) : nullptr;
    const QString current = config ? config->defaultModel : QString();
    m_updatingForm = true;
    m_modelCombo->clear();
    m_modelCombo->addItems(models);
    const int idx = m_modelCombo->findText(current);
    if (idx >= 0)
        m_modelCombo->setCurrentIndex(idx);
    else
        m_modelCombo->setCurrentText(current);
    m_updatingForm = false;
    if (current.isEmpty())
        m_modelCombo->setCurrentIndex(0);
    if (config && config->defaultModel != m_modelCombo->currentText())
        onFieldEdited();
}

void BackendPage::onTestModelsFailed(const QString &error) {
    m_testTimer->stop();
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Connection failed: %1").arg(error));
}

void BackendPage::loadConfigIntoForm(const BackendConfig &config) {
    m_nameEdit->setText(config.name);
    const int kindIndex = m_kindCombo->findData(QVariant::fromValue(static_cast<int>(config.kind)));
    m_kindCombo->setCurrentIndex(kindIndex < 0 ? 0 : kindIndex);
    m_urlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(config.apiKey);
    m_modelCombo->clear();
    m_modelCombo->setCurrentText(config.defaultModel);
    m_enabledBox->setChecked(config.enabled);
}

void BackendPage::saveFormIntoConfig(BackendConfig &config) {
    config.name = m_nameEdit->text().trimmed().isEmpty() ? BackendConfig::kindName(currentKind())
                                                         : m_nameEdit->text().trimmed();
    config.kind = currentKind();
    config.baseUrl = m_urlEdit->text().trimmed();
    // Local servers need an explicit URL; cloud providers use their default.
    if (config.baseUrl.isEmpty() && !BackendConfig::isCloud(config.kind))
        config.baseUrl = BackendConfig::defaultBaseUrl(config.kind);
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.defaultModel = m_modelCombo->currentText().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

void BackendPage::updateUrlState() {
    // Self-hosted servers require an explicit base URL; for cloud providers
    // the built-in endpoint is used, so the field is deactivated.
    m_urlEdit->setEnabled(m_hasSelection && !BackendConfig::isCloud(currentKind()));
    m_urlEdit->setPlaceholderText(BackendConfig::isCloud(currentKind())
                                      ? tr("Custom endpoint (optional)")
                                      : QStringLiteral("http://127.0.0.1:11434"));
}

bool BackendPage::nameFollowsProvider(const QString &name) {
    // The name is considered auto-derived while it is empty or matches any
    // provider name (including the previous one).
    if (name.trimmed().isEmpty())
        return true;
    const auto kinds = BackendConfig::allKinds();
    return std::ranges::any_of(
        kinds, [&](BackendKind kind) { return name.trimmed() == BackendConfig::kindName(kind); });
}

BackendKind BackendPage::currentKind() const {
    return static_cast<BackendKind>(m_kindCombo->currentData().toInt());
}

} // namespace ocapi
