#pragma once

#include "core/mcpmanager.h"
#include "core/mcpserverconfig.h"

#include <QUuid>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QTableWidget;

namespace okapi {

class McpClient;
class McpRegistry;

// Settings UI for MCP servers: add/remove servers, edit connection settings
// (transport, URL or stdio command, arguments, environment, headers), test
// the connection (initialize handshake + feature listing) and manage
// per-tool permissions (off / ask / allow).
class McpPage : public QWidget {
    Q_OBJECT

public:
    explicit McpPage(McpRegistry *registry, McpManager *manager, QWidget *parent = nullptr);

private slots:
    void onAddServer();
    void onRemoveServer();
    void onSelectionChanged();
    void onFieldEdited();
    void onTransportChanged(int index);
    void onTestConnection();
    void onTestConnected(const okapi::McpServerInfo &info);
    void onTestFailed(const QString &error);
    void onTestToolsReady(const QVector<okapi::McpTool> &tools);
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
    // HTTP authorization mode: only the selected field is visible; both
    // values are kept so switching modes loses no configuration.
    QRadioButton *m_apiKeyRadio = nullptr;
    QRadioButton *m_headersRadio = nullptr;
    QLabel *m_apiKeyLabel = nullptr;
    QLabel *m_headersLabel = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
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

    McpClient *m_testClient = nullptr;
    QVector<McpTool> m_testTools;
    bool m_updatingForm = false;
};

} // namespace okapi
