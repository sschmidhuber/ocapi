#include "ui/ttspage.h"
#include "ui_ttspage.h"

#include "core/backendregistry.h"
#include "core/ttsplayer.h"
#include "core/ttsregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QTimer>

#include <algorithm>

namespace okapi {

namespace {

constexpr int TestTimeoutMs = 30000;

} // namespace

TtsPage::TtsPage(TtsRegistry *registry, BackendRegistry *backendRegistry, QWidget *parent)
    : QWidget(parent), m_registry(registry), m_backendRegistry(backendRegistry),
      m_testPlayer(new TtsPlayer(this)), m_ui(std::make_unique<Ui::TtsPage>()) {
    m_ui->setupUi(this);
    m_list = m_ui->list;
    m_speakResponsesBox = m_ui->speakResponsesBox;
    m_nameEdit = m_ui->nameEdit;
    m_formLayout = m_ui->formLayout;
    m_kindCombo = m_ui->kindCombo;
    m_urlEdit = m_ui->urlEdit;
    m_apiKeyEdit = m_ui->apiKeyEdit;
    m_modelEdit = m_ui->modelEdit;
    m_voiceCombo = m_ui->voiceCombo;
    m_speedSlider = m_ui->speedSlider;
    m_speedLabel = m_ui->speedLabel;
    m_speedValueLabel = m_ui->speedValueLabel;
    m_languageEdit = m_ui->languageEdit;
    m_enabledBox = m_ui->enabledBox;
    m_deviceCombo = m_ui->deviceCombo;
    m_testButton = m_ui->testButton;
    m_testResultLabel = m_ui->testResultLabel;
    auto *addButton = m_ui->addButton;
    auto *removeButton = m_ui->removeButton;

    m_speakResponsesBox->setChecked(m_registry->speakResponses());
    for (auto kind : TtsServiceConfig::allKinds()) {
        m_kindCombo->addItem(TtsServiceConfig::kindName(kind),
                             QVariant::fromValue(static_cast<int>(kind)));
    }
    m_urlEdit->setAccessibleName(tr("Base URL"));
    m_voiceCombo->lineEdit()->setPlaceholderText(tr("Voice (optional)"));
    m_voiceCombo->setToolTip(tr("Voice name (OpenAI, Groq), voice ID (Mistral, ElevenLabs)"));
    // Fixed width so the slider keeps its size when the label text changes
    // (e.g. "moderate" vs "very fast" for the Mistral paces).
    m_speedValueLabel->setFixedWidth(
        m_speedValueLabel->fontMetrics().horizontalAdvance(QStringLiteral("very fast")));
    m_testTimer = new QTimer(this);
    m_testTimer->setSingleShot(true);

    m_ui->splitter->setStretchFactor(1, 1);

    connect(addButton, &QPushButton::clicked, this, &TtsPage::onAddService);
    connect(removeButton, &QPushButton::clicked, this, &TtsPage::onRemoveService);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, &TtsPage::onKindChanged);
    connect(m_testButton, &QPushButton::clicked, this, &TtsPage::onTestService);
    connect(m_speakResponsesBox, &QCheckBox::toggled, this, &TtsPage::onSpeakResponsesToggled);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, &TtsPage::onOutputDeviceChanged);
    for (auto *edit : {m_nameEdit, m_urlEdit, m_apiKeyEdit, m_modelEdit, m_languageEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &TtsPage::onFieldEdited);
    }
    connect(m_voiceCombo, &QComboBox::currentTextChanged, this, &TtsPage::onFieldEdited);
    connect(m_voiceCombo->lineEdit(), &QLineEdit::textEdited, this, &TtsPage::onFieldEdited);
    connect(m_speedSlider, &QSlider::valueChanged, this, &TtsPage::onSpeedChanged);
    updateSpeedRange();
    onSpeedChanged(m_speedSlider->value());
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
    m_voiceCombo->setEnabled(hasSelection);
    m_speedSlider->setEnabled(hasSelection);
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
        m_voiceCombo->clear();
        m_voiceCombo->lineEdit()->clear();
        updateSpeedRange();
        m_speedSlider->setValue(m_speedSlider->value()); // re-clamp + relabel
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
    if (m_voiceCombo->currentText().trimmed().isEmpty() ||
        std::ranges::any_of(TtsServiceConfig::allKinds(), [&](TtsServiceKind anyKind) {
            return m_voiceCombo->currentText().trimmed() == TtsServiceConfig::defaultVoice(anyKind);
        }))
        m_voiceCombo->setCurrentText(TtsServiceConfig::defaultVoice(kind));
    populateVoiceCombo();
    updateSpeedRange();
    // Reuse the API key already configured for the same provider's LLM
    // backend when the field is still empty.
    if (m_apiKeyEdit->text().trimmed().isEmpty()) {
        const QString knownKey = knownApiKey();
        if (!knownKey.isEmpty())
            m_apiKeyEdit->setText(knownKey);
    }
    updateUrlState();
    updateLanguageRowState();
    onFieldEdited();
}

void TtsPage::updateLanguageRowState() {
    // The known speech APIs have no language field (Mistral answers 422
    // "extra_forbidden"), so the optional field is only offered for a custom
    // OpenAI-compatible server whose owner knows it accepts it.
    const bool generic = currentKind() == TtsServiceKind::OpenAiTts;
    m_formLayout->setRowVisible(m_languageEdit, generic);
    m_languageEdit->setEnabled(generic);
}

void TtsPage::updateSpeedRange() {
    const bool mistral = currentKind() == TtsServiceKind::Mistral;
    if (mistral) {
        // Mistral only accepts the five discrete [pace=...] values, so the
        // slider steps through them instead of a continuous factor.
        m_speedSlider->setRange(0, static_cast<int>(TtsServiceConfig::availablePaces().size()) - 1);
        const int step =
            static_cast<int>(TtsServiceConfig::paceFromSpeed(TtsServiceConfig::defaultSpeed));
        m_speedSlider->setValue(step);
    } else {
        m_speedSlider->setRange(static_cast<int>(TtsServiceConfig::minSpeed * 100),
                                static_cast<int>(TtsServiceConfig::maxSpeed * 100));
        m_speedSlider->setValue(static_cast<int>(TtsServiceConfig::defaultSpeed * 100));
    }
    onSpeedChanged(m_speedSlider->value());
}

void TtsPage::onSpeedChanged(int value) {
    const bool mistral = currentKind() == TtsServiceKind::Mistral;
    const double speed =
        mistral ? TtsServiceConfig::speedFromPace(static_cast<TtsPace>(value)) : value / 100.0;
    m_speedValueLabel->setText(TtsServiceConfig::speedLabel(speed, currentKind()));
    if (m_updatingForm)
        return;
    auto *item = m_list->currentItem();
    if (!item)
        return;
    auto *config = m_registry->find(QUuid(item->data(Qt::UserRole).toString()));
    if (!config)
        return;
    config->speed = speed;
    m_registry->save();
    emit m_registry->changed();
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
    populateVoiceCombo();
    m_voiceCombo->setCurrentText(config.voice);
    updateSpeedRange();
    const int speedValue =
        currentKind() == TtsServiceKind::Mistral
            ? static_cast<int>(TtsServiceConfig::paceFromSpeed(config.effectiveSpeed()))
            : static_cast<int>(config.effectiveSpeed() * 100);
    m_speedSlider->setValue(speedValue);
    onSpeedChanged(m_speedSlider->value());
    m_languageEdit->setText(config.language);
    updateLanguageRowState();
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
    config.voice = m_voiceCombo->currentText().trimmed();
    config.speed =
        currentKind() == TtsServiceKind::Mistral
            ? TtsServiceConfig::speedFromPace(static_cast<TtsPace>(m_speedSlider->value()))
            : m_speedSlider->value() / 100.0;
    config.language = m_languageEdit->text().trimmed();
    config.enabled = m_enabledBox->isChecked();
}

void TtsPage::populateVoiceCombo() {
    const QString previous = m_voiceCombo->currentText();
    m_voiceCombo->clear();
    for (const QString &voice : TtsServiceConfig::availableVoices(currentKind()))
        m_voiceCombo->addItem(voice);
    // Keep a custom voice ID editable for services without preset voices.
    if (!previous.isEmpty() && m_voiceCombo->findText(previous) < 0)
        m_voiceCombo->addItem(previous);
    m_voiceCombo->setCurrentText(previous);
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
    m_voiceCombo->setVisible(hasVoice);
    const bool hasSpeed = TtsServiceConfig::supportsSpeed(currentKind());
    m_speedSlider->setVisible(hasSpeed);
    m_speedValueLabel->setVisible(hasSpeed);
    m_speedLabel->setVisible(hasSpeed);
    if (auto *form = qobject_cast<QFormLayout *>(m_voiceCombo->parentWidget()->layout()))
        if (auto *label = form->labelForField(m_voiceCombo))
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

} // namespace okapi
