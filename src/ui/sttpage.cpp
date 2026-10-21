#include "ui/sttpage.h"

#include "backends/sttclient.h"
#include "core/audiorecorder.h"
#include "core/sttregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
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

SttPage::SttPage(SttRegistry *registry, QWidget *parent) : QWidget(parent), m_registry(registry) {
    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);

    auto *formWidget = new QWidget(splitter);
    auto *formLayout = new QFormLayout(formWidget);
    m_nameEdit = new QLineEdit(formWidget);
    m_kindCombo = new QComboBox(formWidget);
    for (auto kind : SttServiceConfig::allKinds()) {
        m_kindCombo->addItem(SttServiceConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_urlEdit = new QLineEdit(formWidget);
    m_urlEdit->setAccessibleName(tr("Base URL"));
    m_urlEdit->setPlaceholderText(tr("http://127.0.0.1:8000/v1"));
    m_apiKeyEdit = new QLineEdit(formWidget);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API key"));
    m_modelEdit = new QLineEdit(formWidget);
    m_modelEdit->setPlaceholderText(tr("Model (optional)"));
    m_languageEdit = new QLineEdit(formWidget);
    m_languageEdit->setPlaceholderText(tr("ISO 639-1 code, e.g. en, de (optional)"));
    m_enabledBox = new QCheckBox(tr("Enabled"), formWidget);
    m_enabledBox->setChecked(true);
    m_testButton = new QPushButton(tr("Test service with an audio file\u2026"), formWidget);
    m_testResultLabel = new QLabel(formWidget);
    m_testResultLabel->setWordWrap(true);

    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Type:"), m_kindCombo);
    formLayout->addRow(tr("Base URL:"), m_urlEdit);
    formLayout->addRow(tr("API key:"), m_apiKeyEdit);
    formLayout->addRow(tr("Model:"), m_modelEdit);
    formLayout->addRow(tr("Language:"), m_languageEdit);
    formLayout->addRow(m_enabledBox);
    formLayout->addRow(m_testButton);
    formLayout->addRow(m_testResultLabel);
    m_testResultLabel->hide();

    m_deviceCombo = new QComboBox(formWidget);
    formLayout->addRow(tr("Audio input device:"), m_deviceCombo);

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

    connect(addButton, &QPushButton::clicked, this, &SttPage::onAddService);
    connect(removeButton, &QPushButton::clicked, this, &SttPage::onRemoveService);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, &SttPage::onKindChanged);
    connect(m_testButton, &QPushButton::clicked, this, &SttPage::onTestService);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, &SttPage::onInputDeviceChanged);
    for (auto *edit : {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_modelEdit, m_languageEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &SttPage::onFieldEdited);
    }
    connect(m_enabledBox, &QCheckBox::toggled, this, &SttPage::onFieldEdited);

    populateList();
    populateInputDevices();
    if (!m_registry->services().isEmpty())
        m_list->setCurrentRow(0);
    else
        onSelectionChanged();
}

void SttPage::populateList() {
    m_list->clear();
    for (const auto &config : m_registry->services()) {
        auto *item = new QListWidgetItem(
            config.enabled ? config.name : tr("%1 (disabled)").arg(config.name), m_list);
        item->setData(Qt::UserRole, config.id.toString());
    }
}

void SttPage::populateInputDevices() {
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    m_deviceCombo->addItem(tr("System default"), QString());
    const QStringList ids = AudioRecorder::availableInputDevices();
    const QStringList names = AudioRecorder::availableInputDeviceNames();
    for (int i = 0; i < ids.size() && i < names.size(); ++i)
        m_deviceCombo->addItem(names.at(i), ids.at(i));
    const int index = m_deviceCombo->findData(m_registry->inputDevice());
    m_deviceCombo->setCurrentIndex(index < 0 ? 0 : index);
    m_deviceCombo->blockSignals(false);
}

void SttPage::onAddService() {
    auto &config = m_registry->add(tr("New STT service"), SttServiceKind::WhisperApi);
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

void SttPage::onRemoveService() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (QMessageBox::question(this, tr("Remove STT service"),
                              tr("Remove the service \"%1\"?").arg(item->text())) !=
        QMessageBox::Yes)
        return;
    m_registry->remove(id);
    populateList();
    onSelectionChanged();
}

void SttPage::onSelectionChanged() {
    m_updatingForm = true;
    auto *item = m_list->currentItem();
    const bool hasSelection = item != nullptr;
    m_nameEdit->setEnabled(hasSelection);
    m_kindCombo->setEnabled(hasSelection);
    m_urlEdit->setEnabled(hasSelection);
    m_apiKeyEdit->setEnabled(hasSelection);
    m_modelEdit->setEnabled(hasSelection);
    m_languageEdit->setEnabled(hasSelection);
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
        m_languageEdit->clear();
        m_enabledBox->setChecked(true);
    }
    updateUrlVisibility();
    m_updatingForm = false;
}

void SttPage::onKindChanged(int index) {
    if (m_updatingForm || !m_urlEdit->isEnabled())
        return;
    const auto kind = static_cast<SttServiceKind>(m_kindCombo->itemData(index).toInt());
    if (m_urlEdit->text().isEmpty())
        m_urlEdit->setText(SttServiceConfig::defaultBaseUrl(kind));
    if (m_modelEdit->text().isEmpty())
        m_modelEdit->setText(SttServiceConfig::defaultModel(kind));
    updateUrlVisibility();
    onFieldEdited();
}

void SttPage::onFieldEdited() {
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

void SttPage::onInputDeviceChanged(int index) {
    m_registry->setInputDevice(m_deviceCombo->itemData(index).toString());
}

void SttPage::onTestService() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;

    saveFormIntoConfig(*config);
    m_registry->save();

    if (SttServiceConfig::requiresApiKey(config->kind) && config->apiKey.isEmpty()) {
        m_testResultLabel->setText(tr("Please enter an API key first."));
        m_testResultLabel->show();
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select an audio file"), QString(),
        tr("Audio files (*.wav *.mp3 *.m4a *.ogg *.flac *.webm);;All files (*)"));
    if (filePath.isEmpty())
        return;

    auto *client = new SttClient(*config, this);
    connect(client, &SttClient::transcriptionUpdated, this, &SttPage::onTestReady);
    connect(client, &SttClient::error, this, &SttPage::onTestFailed);
    connect(client, &SttClient::finished, client, &QObject::deleteLater);
    connect(client, &SttClient::error, client, [client]() { client->deleteLater(); });

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_testResultLabel->setText(tr("Could not read the selected file."));
        return;
    }
    const QByteArray audio = file.readAll();
    file.close();

    m_testButton->setEnabled(false);
    m_testResultLabel->setText(tr("Testing service…"));
    m_testResultLabel->show();
    client->start();
    client->transcribeChunk(audio, false);
    client->finish();
}

void SttPage::onTestReady(const QString &text) {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(text.trimmed().isEmpty()
                                   ? tr("Service reachable, but no text was transcribed.")
                                   : tr("Transcription successful:\n%1").arg(text));
}

void SttPage::onTestFailed(const QString &error) {
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Test failed: %1").arg(error));
}

void SttPage::loadConfigIntoForm(const SttServiceConfig &config) {
    m_nameEdit->setText(config.name);
    const int kindIndex = m_kindCombo->findData(QVariant::fromValue(static_cast<int>(config.kind)));
    m_kindCombo->setCurrentIndex(kindIndex < 0 ? 0 : kindIndex);
    m_urlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(config.apiKey);
    m_modelEdit->setText(config.model);
    m_languageEdit->setText(config.language);
    m_enabledBox->setChecked(config.enabled);
}

void SttPage::saveFormIntoConfig(SttServiceConfig &config) {
    config.name = m_nameEdit->text().trimmed().isEmpty() ? SttServiceConfig::kindName(config.kind)
                                                         : m_nameEdit->text().trimmed();
    config.kind = currentKind();
    config.baseUrl = m_urlEdit->text().trimmed();
    if (config.baseUrl.isEmpty() && !SttServiceConfig::isCloud(config.kind))
        config.baseUrl = SttServiceConfig::defaultBaseUrl(config.kind);
    config.apiKey = m_apiKeyEdit->text().trimmed();
    config.model = m_modelEdit->text().trimmed();
    config.language = m_languageEdit->text().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

void SttPage::updateUrlVisibility() {
    const bool cloud = SttServiceConfig::isCloud(currentKind());
    const bool visible = !cloud || !m_urlEdit->text().trimmed().isEmpty();
    m_urlEdit->setVisible(visible);
    m_urlEdit->setPlaceholderText(cloud ? tr("Custom endpoint (optional)")
                                        : QStringLiteral("http://127.0.0.1:8000/v1"));
    if (auto *form = qobject_cast<QFormLayout *>(m_urlEdit->parentWidget()->layout()))
        if (auto *label = form->labelForField(m_urlEdit))
            label->setVisible(visible);
}

SttServiceKind SttPage::currentKind() const {
    return static_cast<SttServiceKind>(m_kindCombo->currentData().toInt());
}

} // namespace ocapi
