#pragma once

#include "core/mcpmanager.h"
#include "core/mcpserverconfig.h"

#include <QDialog>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace ocapi {

class McpClient;
class McpRegistry;

// Settings UI for MCP servers: add/remove servers, edit connection settings
// (transport, URL or stdio command, arguments, environment, headers), test
// the connection (initialize handshake + feature listing) and manage
// per-tool permissions (off / ask / allow).
class McpDialog : public QDialog {
    Q_OBJECT

public:
    explicit McpDialog(McpRegistry *registry, McpManager *manager, QWidget *parent = nullptr);

private slots:
    void onAddServer();
    void onRemoveServer();
    void onSelectionChanged();
    void onFieldEdited();
    void onTransportChanged(int index);
    void onTestConnection();
    void onTestConnected(const ocapi::McpServerInfo &info);
    void onTestFailed(const QString &error);
    void onTestToolsReady(const QVector<ocapi::McpTool> &tools);
    void onPermissionChanged(int row, int column);

private:
    void populateList();
    void loadConfigIntoForm(const McpServerConfig &config);
    void saveFormIntoConfig(McpServerConfig &config);
    void updateTransportVisibility();
    void refreshCapabilities(const McpServerInfo &info, const QVector<McpTool> &tools);
    void refreshToolPermissions(const McpServerConfig &config);
    [[nodiscard]] McpServerConfig *currentConfig();

    McpRegistry *m_registry;
    McpManager *m_manager;

    QListWidget *m_list = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_transportCombo = nullptr;
    QStackedWidget *m_connectionStack = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QLineEdit *m_argumentsEdit = nullptr;
    QLineEdit *m_environmentEdit = nullptr;
    QLineEdit *m_headersEdit = nullptr;
    QComboBox *m_defaultPermissionCombo = nullptr;
    QCheckBox *m_enabledBox = nullptr;
    QPushButton *m_testButton = nullptr;
    QLabel *m_testResultLabel = nullptr;
    QLabel *m_serverInfoLabel = nullptr;
    QTableWidget *m_toolsTable = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    McpClient *m_testClient = nullptr;
    QVector<McpTool> m_testTools;
    bool m_updatingForm = false;
};

} // namespace ocapi
