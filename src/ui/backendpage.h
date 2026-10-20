#pragma once

#include "core/backendconfig.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QPushButton;

namespace ocapi {

class BackendRegistry;
class IBackend;

// Embedded settings page for model backends: add/remove backends, edit
// connection settings (type, base URL, API key, default model, enabled) and
// test the connection by listing the backend's models.
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

private:
    void populateList();
    void loadConfigIntoForm(const BackendConfig &config);
    void saveFormIntoConfig(BackendConfig &config);
    void updateUrlVisibility();
    [[nodiscard]] BackendKind currentKind() const;

    BackendRegistry *m_registry;
    QListWidget *m_list;
    QLineEdit *m_nameEdit;
    QComboBox *m_kindCombo;
    QLineEdit *m_urlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QCheckBox *m_enabledBox;
    QPushButton *m_testButton;
    QLabel *m_testResultLabel;
    IBackend *m_testBackend = nullptr;
    bool m_updatingForm = false;
};

} // namespace ocapi
