#include "ui/ttspage.h"

#include "core/backendregistry.h"
#include "core/ttsplayer.h"
#include "core/ttsregistry.h"

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

constexpr int TestTimeoutMs = 30000;

} // namespace

TtsPage::TtsPage(TtsRegistry *registry, BackendRegistry *backendRegistry, QWidget *parent)
    : QWidget(parent), m_registry(registry), m_backendRegistry(backendRegistry),
      m_testPlayer(new TtsPlayer(this)) {
    auto *splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(200);

    auto *formWidget = new QWidget(splitter);
    auto *formLayout = new QFormLayout(formWidget);
    m_speakResponsesBox = new QCheckBox(tr("Speak assistant responses aloud"), formWidget);
    m_speakResponsesBox->setChecked(m_registry->speakResponses());
    m_nameEdit = new QLineEdit(formWidget);
    m_kindCombo = new QComboBox(formWidget);
    for (auto kind : TtsServiceConfig::allKinds()) {
        m_kindCombo->addItem(TtsServiceConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_urlEdit = new QLineEdit(formWidget);
    m_urlEdit->setAccessibleName(tr("Base URL"));
    m_urlEdit->setPlaceholderText(tr("http://127.0.0.1:5000"));
    m_apiKeyEdit = new QLineEdit(formWidget);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API key"));
    m_modelEdit = new QLineEdit(formWidget);
    m_modelEdit->setPlaceholderText(tr("Model (optional)"));
    m_voiceEdit = new QLineEdit(formWidget);
    m_voiceEdit->setPlaceholderText(tr("Voice (optional)"));
    m_languageEdit = new QLineEdit(formWidget);
    m_languageEdit->setPlaceholderText(tr("ISO 639-1 code, e.g. en, de (optional)"));
    m_enabledBox = new QCheckBox(tr("Enabled"), formWidget);
    m_enabledBox->setChecked(true);
    m_testButton = new QPushButton(tr("Test voice"), formWidget);
    m_testResultLabel = new QLabel(formWidget);
    m_testResultLabel->setWordWrap(true);
    m_testTimer = new QTimer(this);
    m_testTimer->setSingleShot(true);

    formLayout->addRow(m_speakResponsesBox);
    formLayout->addRow(tr("Provider:"), m_kindCombo);
    formLayout->addRow(tr("Name:"), m_nameEdit);
    formLayout->addRow(tr("Base URL:"), m_urlEdit);
    formLayout->addRow(tr("API key:"), m_apiKeyEdit);
    formLayout->addRow(tr("Default model:"), m_modelEdit);
    formLayout->addRow(tr("Voice:"), m_voiceEdit);
    formLayout->addRow(tr("Language:"), m_languageEdit);
    formLayout->addRow(m_enabledBox);
    formLayout->addRow(m_testButton);
    formLayout->addRow(m_testResultLabel);
    m_testResultLabel->hide();

    m_deviceCombo = new QComboBox(formWidget);
    formLayout->addRow(tr("Audio output device:"), m_deviceCombo);

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

    connect(addButton, &QPushButton::clicked, this, &TtsPage::onAddService);
    connect(removeButton, &QPushButton::clicked, this, &TtsPage::onRemoveService);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, &TtsPage::onKindChanged);
    connect(m_testButton, &QPushButton::clicked, this, &TtsPage::onTestService);
    connect(m_speakResponsesBox, &QCheckBox::toggled, this, &TtsPage::onSpeakResponsesToggled);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, &TtsPage::onOutputDeviceChanged);
    for (auto *edit :
         {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_modelEdit, m_voiceEdit, m_languageEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &TtsPage::onFieldEdited);
    }
    connect(m_enabledBox, &QCheckBox::toggled, this, &TtsPage::onFieldEdited);
    connect(m_testPlayer, &TtsPlayer::speakingChanged, this, &TtsPage::onTestSpeakingChanged);
    connect(m_testPlayer, &TtsPlayer::error, this, &TtsPage::onTestError);
    connect(m_testTimer, &QTimer::timeout, this, &TtsPage::onTestTimeout);

    populateList();
    populateOutputDevices();
    if (!m_registry->services().isEmpty())
        m_list->setCurrentRow(0);
    else
        onSelectionChanged();
}

TtsPage::~TtsPage() {
    m_testPlayer->stop();
}

void TtsPage::populateList() {
    m_list->clear();
    for (const auto &config : m_registry->services()) {
        auto *item = new QListWidgetItem(
            config.enabled ? config.name : tr("%1 (disabled)").arg(config.name), m_list);
        item->setData(Qt::UserRole, config.id.toString());
    }
}

void TtsPage::populateOutputDevices() {
    m_deviceCombo->blockSignals(true);
    m_deviceCombo->clear();
    m_deviceCombo->addItem(tr("System default"), QString());
    const QStringList ids = TtsPlayer::availableOutputDevices();
    const QStringList names = TtsPlayer::availableOutputDeviceNames();
    for (int i = 0; i < ids.size() && i < names.size(); ++i)
        m_deviceCombo->addItem(names.at(i), ids.at(i));
    const int index = m_deviceCombo->findData(m_registry->outputDevice());
    m_deviceCombo->setCurrentIndex(index < 0 ? 0 : index);
    m_deviceCombo->blockSignals(false);
}

void TtsPage::onAddService() {
    const auto kind = TtsServiceKind::OpenAiTts;
    auto &config = m_registry->add(TtsServiceConfig::kindName(kind), kind);
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

void TtsPage::onRemoveService() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    if (QMessageBox::question(this, tr("Remove TTS service"),
                              tr("Remove the service \"%1\"?").arg(item->text())) !=
        QMessageBox::Yes)
        return;
    m_registry->remove(id);
    populateList();
    onSelectionChanged();
}

void TtsPage::onSelectionChanged() {
    m_updatingForm = true;
    auto *item = m_list->currentItem();
    const bool hasSelection = item != nullptr;
    m_hasSelection = hasSelection;
    m_nameEdit->setEnabled(hasSelection);
    m_kindCombo->setEnabled(hasSelection);
    m_urlEdit->setEnabled(hasSelection);
    m_apiKeyEdit->setEnabled(hasSelection);
    m_modelEdit->setEnabled(hasSelection);
    m_voiceEdit->setEnabled(hasSelection);
    m_languageEdit->setEnabled(hasSelection);
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
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_apiKeyEdit->clear();
        m_modelEdit->clear();
        m_voiceEdit->clear();
        m_languageEdit->clear();
        m_enabledBox->setChecked(true);
    }
    updateUrlState();
    m_updatingForm = false;
}

void TtsPage::onKindChanged(int index) {
    if (m_updatingForm || !m_kindCombo->isEnabled())
        return;
    const auto kind = static_cast<TtsServiceKind>(m_kindCombo->itemData(index).toInt());
    // Follow the provider name while the user has not chosen a custom name,
    // like the LLM settings page does.
    if (m_nameEdit->text().trimmed().isEmpty() ||
        std::ranges::any_of(TtsServiceConfig::allKinds(), [&](TtsServiceKind anyKind) {
            return m_nameEdit->text().trimmed() == TtsServiceConfig::kindName(anyKind);
        }))
        m_nameEdit->setText(TtsServiceConfig::kindName(kind));
    // Self-hosted servers get the provider's default URL prefilled; cloud
    // providers use their built-in endpoint and the field is disabled.
    if (TtsServiceConfig::isCloud(kind))
        m_urlEdit->clear();
    else if (m_urlEdit->text().isEmpty())
        m_urlEdit->setText(TtsServiceConfig::defaultBaseUrl(kind));
    if (m_modelEdit->text().trimmed().isEmpty() ||
        std::ranges::any_of(TtsServiceConfig::allKinds(), [&](TtsServiceKind anyKind) {
            return m_modelEdit->text().trimmed() == TtsServiceConfig::defaultModel(anyKind);
        }))
        m_modelEdit->setText(TtsServiceConfig::defaultModel(kind));
    if (m_voiceEdit->text().trimmed().isEmpty() && !TtsServiceConfig::defaultVoice(kind).isEmpty())
        m_voiceEdit->setText(TtsServiceConfig::defaultVoice(kind));
    // Reuse the API key already configured for the same provider's LLM
    // backend when the field is still empty.
    if (m_apiKeyEdit->text().trimmed().isEmpty()) {
        const QString knownKey = knownApiKey();
        if (!knownKey.isEmpty())
            m_apiKeyEdit->setText(knownKey);
    }
    updateUrlState();
    onFieldEdited();
}

void TtsPage::onFieldEdited() {
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

void TtsPage::onSpeakResponsesToggled(bool enabled) {
    m_registry->setSpeakResponses(enabled);
}

void TtsPage::onOutputDeviceChanged(int index) {
    m_registry->setOutputDevice(m_deviceCombo->itemData(index).toString());
}

void TtsPage::onTestService() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;

    saveFormIntoConfig(*config);
    m_registry->save();

    if (TtsServiceConfig::requiresApiKey(config->kind) && config->apiKey.isEmpty()) {
        m_testResultLabel->setText(tr("Please enter an API key first."));
        m_testResultLabel->show();
        return;
    }

    m_testPlayer->setService(*config);
    m_testPlayer->setOutputDevice(m_registry->outputDevice());
    m_testButton->setEnabled(false);
    m_testResultLabel->setText(tr("Synthesizing sample sentence\u2026"));
    m_testResultLabel->show();
    // Safety net: playback ends via speakingChanged, but when synthesis
    // never produces audio the player never starts speaking and no signal
    // arrives; the timer keeps the page from being stuck in test mode.
    m_testTimer->start(TestTimeoutMs);
    m_testPlayer->feedText(tr("This is a test of the text to speech service."));
    m_testPlayer->endOfText();
}

void TtsPage::onTestSpeakingChanged(bool speaking) {
    if (speaking)
        return;
    m_testTimer->stop();
    m_testButton->setEnabled(true);
    // An error was already reported and shown; do not overwrite it with a
    // generic "Playback finished." (e.g. when synthesis failed and no audio
    // ever played).
    if (m_testResultLabel->text().startsWith(tr("Test failed")))
        return;
    m_testResultLabel->setText(tr("Playback finished."));
}

void TtsPage::onTestError(const QString &message) {
    m_testTimer->stop();
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(tr("Test failed: %1").arg(message));
}

void TtsPage::onTestTimeout() {
    m_testPlayer->stop();
    m_testButton->setEnabled(true);
    m_testResultLabel->setText(
        tr("Test failed: no response within %1 seconds.").arg(TestTimeoutMs / 1000));
}

void TtsPage::loadConfigIntoForm(const TtsServiceConfig &config) {
    m_nameEdit->setText(config.name);
    const int kindIndex = m_kindCombo->findData(QVariant::fromValue(static_cast<int>(config.kind)));
    m_kindCombo->setCurrentIndex(kindIndex < 0 ? 0 : kindIndex);
    m_urlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(config.apiKey);
    m_modelEdit->setText(config.model);
    m_voiceEdit->setText(config.voice);
    m_languageEdit->setText(config.language);
    m_enabledBox->setChecked(config.enabled);
}

void TtsPage::saveFormIntoConfig(TtsServiceConfig &config) {
    config.name = m_nameEdit->text().trimmed().isEmpty() ? TtsServiceConfig::kindName(config.kind)
                                                         : m_nameEdit->text().trimmed();
    config.kind = currentKind();
    config.baseUrl = m_urlEdit->text().trimmed();
    if (config.baseUrl.isEmpty() && !TtsServiceConfig::isCloud(config.kind))
        config.baseUrl = TtsServiceConfig::defaultBaseUrl(config.kind);
    config.apiKey = m_apiKeyEdit->text().trimmed();
    // The model stays empty unless the user picks another one: the config
    // falls back to the provider's default model.
    config.model = m_modelEdit->text().trimmed();
    if (config.model.isEmpty() ||
        std::ranges::any_of(TtsServiceConfig::allKinds(), [&](TtsServiceKind anyKind) {
            return config.model == TtsServiceConfig::defaultModel(anyKind);
        }))
        config.model = TtsServiceConfig::defaultModel(config.kind);
    config.voice = m_voiceEdit->text().trimmed();
    config.language = m_languageEdit->text().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

void TtsPage::updateUrlState() {
    // Self-hosted servers require an explicit base URL; for cloud providers
    // the built-in endpoint is used and the field is disabled, like the LLM
    // settings page.
    const bool cloud = TtsServiceConfig::isCloud(currentKind());
    m_urlEdit->setEnabled(m_hasSelection && !cloud);
    m_urlEdit->setPlaceholderText(cloud ? tr("Custom endpoint (optional)")
                                        : QStringLiteral("http://127.0.0.1:5000"));

    // Piper has no voice concept (the model file selects the voice).
    const bool hasVoice = currentKind() != TtsServiceKind::Piper;
    m_voiceEdit->setVisible(hasVoice);
    if (auto *form = qobject_cast<QFormLayout *>(m_voiceEdit->parentWidget()->layout()))
        if (auto *label = form->labelForField(m_voiceEdit))
            label->setVisible(hasVoice);
}

QString TtsPage::knownApiKey() const {
    // The API key of an LLM backend configured for the same provider: the
    // TTS and chat APIs share one account.
    const auto provider = TtsServiceConfig::llmProviderKind(currentKind());
    if (!m_backendRegistry || !provider)
        return {};
    for (const auto &config : m_backendRegistry->backends()) {
        if (config.kind == *provider && !config.apiKey.trimmed().isEmpty())
            return config.apiKey.trimmed();
    }
    return {};
}

TtsServiceKind TtsPage::currentKind() const {
    return static_cast<TtsServiceKind>(m_kindCombo->currentData().toInt());
}

} // namespace ocapi
