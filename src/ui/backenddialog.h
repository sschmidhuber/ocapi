#pragma once

#include "core/backendconfig.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QListWidget;
class QCheckBox;

namespace ocapi {

class BackendRegistry;

class BackendDialog : public QDialog {
    Q_OBJECT

public:
    explicit BackendDialog(BackendRegistry *registry, QWidget *parent = nullptr);

private slots:
    void onAddBackend();
    void onRemoveBackend();
    void onSelectionChanged();
    void onKindChanged(int index);
    void onFieldEdited();

private:
    void populateList();
    void loadConfigIntoForm(const BackendConfig &config);
    void saveFormIntoConfig(BackendConfig &config);
    BackendKind currentKind() const;

    BackendRegistry *m_registry;
    QListWidget *m_list;
    QLineEdit *m_nameEdit;
    QComboBox *m_kindCombo;
    QLineEdit *m_urlEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QCheckBox *m_enabledBox;
    QDialogButtonBox *m_buttonBox;
    bool m_updatingForm = false;
};

} // namespace ocapi
