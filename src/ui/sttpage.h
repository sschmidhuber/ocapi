#pragma once

#include "core/sttconfig.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace ocapi {

class BackendRegistry;
class SttRegistry;

// Embedded settings page for speech-to-text: manage transcription services
// (type, base URL, API key, model, language, enabled), select the audio
// input device and test a service by uploading a short sample recording.
class SttPage : public QWidget {
    Q_OBJECT

public:
    explicit SttPage(SttRegistry *registry, BackendRegistry *backendRegistry = nullptr,
                     QWidget *parent = nullptr);

private slots:
    void onAddService();
    void onRemoveService();
    void onSelectionChanged();
    void onKindChanged(int index);
    void onFieldEdited();
    void onInputDeviceChanged(int index);
    void onTestService();
    void onTestReady(const QString &text);
    void onTestFailed(const QString &error);

private:
    void populateList();
    void populateInputDevices();
    void loadConfigIntoForm(const SttServiceConfig &config);
    void saveFormIntoConfig(SttServiceConfig &config);
    void updateUrlState();
    [[nodiscard]] QString knownApiKey() const;
    [[nodiscard]] SttServiceKind currentKind() const;

    SttRegistry *m_registry;
    BackendRegistry *m_backendRegistry;
    QListWidget *m_list;
    QLineEdit *m_nameEdit;
    QComboBox *m_kindCombo;
    QLineEdit *m_urlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QLineEdit *m_languageEdit;
    QSpinBox *m_chunkDurationSpin;
    QCheckBox *m_enabledBox;
    QComboBox *m_deviceCombo;
    QPushButton *m_testButton;
    QLabel *m_testResultLabel;
    bool m_updatingForm = false;
    bool m_hasSelection = false;
};

} // namespace ocapi
