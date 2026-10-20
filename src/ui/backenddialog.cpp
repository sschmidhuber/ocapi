#include "ui/backenddialog.h"
#include "core/backendregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
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

namespace {

const QVector<BackendKind>& allKinds()
{
    static const QVector<BackendKind> kinds{
        BackendKind::Ollama,
        BackendKind::LlamaCpp,
        BackendKind::Vllm,
        BackendKind::OpenAi,
        BackendKind::Anthropic,
        BackendKind::Gemini,
        BackendKind::Mistral,
    };
    return kinds;
}

} // namespace

BackendDialog::BackendDialog(BackendRegistry* registry, QWidget* parent)
    : QDialog(parent)
    , m_registry(registry)
{
    setWindowTitle(tr("Manage backends"));
    resize(720, 420);

    auto* splitter = new QSplitter(this);

    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);

    auto* formWidget = new QWidget(splitter);
    auto* formLayout = new QFormLayout(formWidget);

    m_nameEdit = new QLineEdit(formWidget);
    m_kindCombo = new QComboBox(formWidget);
    for (auto kind : allKinds()) {
        m_kindCombo->addItem(BackendConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_urlEdit = new QLineEdit(formWidget);
    m_urlEdit->setPlaceholderText(tr("http://127.0.0.1:11434"));
    m_apiKeyEdit = new QLineEdit(formWidget);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("Leave empty for local servers without authentication"));
    m_modelEdit = new QLineEdit(formWidget);
    m_modelEdit->setPlaceholderText(tr("Default model (optional)"));
    m_enabledBox = new QCheckBox(tr("Enabled"), formWidget);
    m_enabledBox->setChecked(true);

    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Type:"), m_kindCombo);
    formLayout->addRow(tr("Base URL:"), m_urlEdit);
    formLayout->addRow(tr("API key:"), m_apiKeyEdit);
    formLayout->addRow(tr("Default model:"), m_modelEdit);
    formLayout->addRow(m_enabledBox);

    auto* listButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add"), splitter->parentWidget() ? nullptr : formWidget);
    addButton->setParent(m_list->parentWidget());
    auto* removeButton = new QPushButton(tr("Remove"));
    removeButton->setParent(m_list->parentWidget());
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch();

    auto* leftLayout = new QVBoxLayout;
    leftLayout->addWidget(m_list);
    leftLayout->addLayout(listButtons);
    auto* leftWidget = new QWidget(splitter);
    leftWidget->setLayout(leftLayout);
    splitter->addWidget(leftWidget);
    splitter->addWidget(formWidget);
    splitter->setStretchFactor(1, 1);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->addWidget(splitter);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outerLayout->addWidget(m_buttonBox);

    connect(addButton, &QPushButton::clicked, this, &BackendDialog::onAddBackend);
    connect(removeButton, &QPushButton::clicked, this, &BackendDialog::onRemoveBackend);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        onSelectionChanged();
        Q_UNUSED(row)
    });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, &BackendDialog::onKindChanged);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (auto* edit : {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_modelEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &BackendDialog::onFieldEdited);
    }
    connect(m_enabledBox, &QCheckBox::toggled, this, &BackendDialog::onFieldEdited);

    populateList();
    if (!m_registry->backends().isEmpty())
        m_list->setCurrentRow(0);
    else
        onSelectionChanged();
}

void BackendDialog::populateList()
{
    m_list->clear();
    for (const auto& config : m_registry->backends()) {
        auto* item = new QListWidgetItem(
            config.enabled ? config.name
                           : tr("%1 (disabled)").arg(config.name),
            m_list);
        item->setData(Qt::UserRole, config.id.toString());
    }
}

void BackendDialog::onAddBackend()
{
    auto& config = m_registry->add(tr("New backend"), BackendKind::Ollama);
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

void BackendDialog::onRemoveBackend()
{
    auto* item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (QMessageBox::question(this, tr("Remove backend"),
                              tr("Remove the backend \"%1\"?").arg(item->text()))
        != QMessageBox::Yes)
        return;
    m_registry->remove(id);
    m_registry->save();
    populateList();
    onSelectionChanged();
}

void BackendDialog::onSelectionChanged()
{
    m_updatingForm = true;
    auto* item = m_list->currentItem();
    const bool hasSelection = item != nullptr;
    m_nameEdit->setEnabled(hasSelection);
    m_kindCombo->setEnabled(hasSelection);
    m_urlEdit->setEnabled(hasSelection);
    m_apiKeyEdit->setEnabled(hasSelection);
    m_modelEdit->setEnabled(hasSelection);
    m_enabledBox->setEnabled(hasSelection);
    if (hasSelection) {
        const auto* config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
        if (config)
            loadConfigIntoForm(*config);
    } else {
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_apiKeyEdit->clear();
        m_modelEdit->clear();
        m_enabledBox->setChecked(true);
    }
    m_updatingForm = false;
}

void BackendDialog::onKindChanged(int index)
{
    if (m_updatingForm || !m_urlEdit->isEnabled())
        return;
    const auto kind = static_cast<BackendKind>(m_kindCombo->itemData(index).toInt());
    // Only fill in the default URL when the field is empty or still holds
    // another provider's default, to avoid clobbering user input.
    if (m_urlEdit->text().isEmpty())
        m_urlEdit->setText(BackendConfig::defaultBaseUrl(kind));
    onFieldEdited();
}

void BackendDialog::onFieldEdited()
{
    if (m_updatingForm)
        return;
    auto* item = m_list->currentItem();
    if (!item)
        return;
    auto* config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;
    saveFormIntoConfig(*config);
    item->setText(config->enabled ? config->name
                                  : tr("%1 (disabled)").arg(config->name));
    m_registry->save();
    emit m_registry->changed();
}

void BackendDialog::loadConfigIntoForm(const BackendConfig& config)
{
    m_nameEdit->setText(config.name);
    const int kindIndex = m_kindCombo->findData(QVariant::fromValue(static_cast<int>(config.kind)));
    m_kindCombo->setCurrentIndex(kindIndex < 0 ? 0 : kindIndex);
    m_urlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(config.apiKey);
    m_modelEdit->setText(config.defaultModel);
    m_enabledBox->setChecked(config.enabled);
}

void BackendDialog::saveFormIntoConfig(BackendConfig& config)
{
    config.name = m_nameEdit->text().trimmed().isEmpty()
        ? BackendConfig::kindName(config.kind)
        : m_nameEdit->text().trimmed();
    config.kind = currentKind();
    config.baseUrl = m_urlEdit->text().trimmed();
    if (config.baseUrl.isEmpty())
        config.baseUrl = BackendConfig::defaultBaseUrl(config.kind);
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.defaultModel = m_modelEdit->text().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

BackendKind BackendDialog::currentKind() const
{
    return static_cast<BackendKind>(m_kindCombo->currentData().toInt());
}

} // namespace ocapi
