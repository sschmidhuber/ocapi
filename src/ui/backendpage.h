#pragma once

#include "core/backendconfig.h"

#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QTimer;

namespace ocapi {

class BackendRegistry;
class IBackend;

// Embedded settings page for model backends: add/remove backends, edit
// connection settings (provider, base URL, API key, context window, enabled)
// and test the connection by listing the backend's models. The model is
// chosen in the main chat window's model picker, not here.
class BackendPage : public QWidget {
    Q_OBJECT

public:
    explicit BackendPage(BackendRegistry *registry, QWidget *parent = nullptr);

private slots:
    void onAddBackend();
    void onRemoveBackend();
    void onSelectionChanged();
    void onKindChanged(int index);
    void onFieldEdited();
    void onTestConnection();
    void onTestModelsReady(const QStringList &models);
    void onTestModelsFailed(const QString &error);
    void onTestTimeout();

private:
    void populateList();
    void loadConfigIntoForm(const BackendConfig &config);
    void saveFormIntoConfig(BackendConfig &config);
    void updateUrlState();
    void updateContextWindowState();
    [[nodiscard]] static bool nameFollowsProvider(const QString &name);
    [[nodiscard]] BackendKind currentKind() const;

    BackendRegistry *m_registry;
    QFormLayout *m_formLayout = nullptr;
    QListWidget *m_list;
    QComboBox *m_kindCombo;
    QLineEdit *m_nameEdit;
    QLineEdit *m_urlEdit;
    QLineEdit *m_apiKeyEdit;
    QSpinBox *m_contextSpin = nullptr;
    QCheckBox *m_enabledBox;
    QPushButton *m_testButton;
    QLabel *m_testResultLabel;
    IBackend *m_testBackend = nullptr;
    QTimer *m_testTimer = nullptr;
    bool m_hasSelection = false;
    bool m_updatingForm = false;
};

} // namespace ocapi
