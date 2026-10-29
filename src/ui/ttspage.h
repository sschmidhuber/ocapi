#pragma once

#include "core/ttsconfig.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace ocapi {

class BackendRegistry;
class TtsPlayer;
class TtsRegistry;

// Embedded settings page for text-to-speech: manage synthesis services
// (provider, base URL, API key, model, voice, language, enabled), toggle
// speaking assistant responses aloud, select the audio output device and
// test a service by synthesizing and playing a sample sentence.
class TtsPage : public QWidget {
    Q_OBJECT

public:
    explicit TtsPage(TtsRegistry *registry, BackendRegistry *backendRegistry = nullptr,
                     QWidget *parent = nullptr);
    ~TtsPage() override;

private slots:
    void onAddService();
    void onRemoveService();
    void onSelectionChanged();
    void onKindChanged(int index);
    void onFieldEdited();
    void onSpeakResponsesToggled(bool enabled);
    void onOutputDeviceChanged(int index);
    void onTestService();
    void onTestSpeakingChanged(bool speaking);
    void onTestError(const QString &message);
    void onTestTimeout();

private:
    void populateList();
    void populateOutputDevices();
    void loadConfigIntoForm(const TtsServiceConfig &config);
    void saveFormIntoConfig(TtsServiceConfig &config);
    void updateUrlState();
    [[nodiscard]] QString knownApiKey() const;
    [[nodiscard]] TtsServiceKind currentKind() const;

    TtsRegistry *m_registry;
    BackendRegistry *m_backendRegistry;
    TtsPlayer *m_testPlayer = nullptr;
    QListWidget *m_list;
    QCheckBox *m_speakResponsesBox;
    QLineEdit *m_nameEdit;
    QComboBox *m_kindCombo;
    QLineEdit *m_urlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QLineEdit *m_voiceEdit;
    QLineEdit *m_languageEdit;
    QCheckBox *m_enabledBox;
    QComboBox *m_deviceCombo;
    QPushButton *m_testButton;
    QLabel *m_testResultLabel;
    QTimer *m_testTimer = nullptr;
    bool m_updatingForm = false;
    bool m_hasSelection = false;
};

} // namespace ocapi
