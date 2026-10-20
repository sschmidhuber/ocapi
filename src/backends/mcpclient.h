#pragma once

#include "core/mcpprotocol.h"
#include "core/mcpserverconfig.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace ocapi {

// Speaks the Model Context Protocol to one MCP server over stdio or HTTP.
//
// The client implements the full MCP feature set the UI exposes: initialize
// handshake with capability negotiation, tools (list + call), resources
// (list, templates, read), prompts (list + get), logging notifications and
// server->client requests (sampling/createMessage, roots/list) which are
// answered with a capability error because ocapi never samples. Both the
// Streamable HTTP transport (single POST, JSON or SSE response) and the
// legacy HTTP+SSE transport (POST to send, GET for the event stream) are
// supported; stdio spawns the configured command as a child process.
//
// All operations are asynchronous; results are delivered through signals.
class McpClient : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Failed,
    };

    explicit McpClient(McpServerConfig config, QObject *parent = nullptr);
    ~McpClient() override;

    McpClient(const McpClient &) = delete;
    McpClient &operator=(const McpClient &) = delete;

    [[nodiscard]] const McpServerConfig &config() const { return m_config; }
    [[nodiscard]] McpServerConfig &configRef() { return m_config; }
    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] const McpServerInfo &serverInfo() const { return m_serverInfo; }

    // Starts the transport and runs the MCP initialize handshake.
    void connectToServer();
    void disconnectFromServer();

    [[nodiscard]] bool isConnected() const { return m_state == State::Connected; }

    // Feature queries; results arrive via the corresponding signals. Calling
    // them while not connected fails immediately.
    void listTools();
    void listResources();
    void listResourceTemplates();
    void listPrompts();
    void readResource(const QString &uri);
    void getPrompt(const QString &name, const QJsonObject &arguments = {});
    void callTool(const QString &name, const QJsonObject &arguments);

    // Cancels in-flight requests and reports failure for the pending call.
    void cancelPending();

signals:
    void stateChanged(ocapi::McpClient::State state);
    void connected(const ocapi::McpServerInfo &info);
    void connectionFailed(const QString &error);

    void toolsReady(const QVector<ocapi::McpTool> &tools);
    void resourcesReady(const QVector<ocapi::McpResource> &resources);
    void resourceTemplatesReady(const QVector<ocapi::McpResourceTemplate> &templs);
    void promptsReady(const QVector<ocapi::McpPrompt> &prompts);
    void resourceRead(const QString &uri, const QString &mimeType, const QString &text);
    void promptReady(const QString &name, const ocapi::McpPromptContent &content);
    void toolResult(const QString &toolName, const ocapi::McpToolResult &result);
    void requestFailed(const QString &method, const QString &error);

    // Server-initiated notifications, surfaced so the UI can show them.
    void logMessage(const QString &level, const QString &logger, const QString &message);
    void serverNotification(const QString &method, const QJsonObject &params);
    // Server-initiated requests the client must answer (sampling, roots).
    void serverRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);

private:
    struct PendingCall {
        QString method;
        QString requestId;
    };

    void setState(State state);
    void fail(const QString &error);

    // Transport setup/teardown.
    void startStdio();
    void startHttp();
    void stopTransport();

    // JSON-RPC plumbing.
    QJsonValue sendRequest(const QString &method, const QJsonObject &params);
    void sendNotification(const QString &method, const QJsonObject &params);
    void sendMessage(const QJsonObject &message);
    void sendHttpPost(const QJsonObject &message);
    void handleIncoming(const QByteArray &payload);
    void handleJsonRpcMessage(const QJsonObject &message);
    void handleResponse(const QJsonObject &message);
    void handleServerRequest(const QJsonValue &id, const QString &method,
                             const QJsonObject &params);
    void respondError(const QJsonValue &id, int code, const QString &text);
    void respondResult(const QJsonValue &id, const QJsonObject &result);

    // Stdio transport.
    void onProcessReadyRead();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

    // HTTP transports.
    void onPostFinished(QNetworkReply *reply, const QString &requestId);
    void onSseEvent(const QByteArray &event, const QByteArray &data);

    // Handshake.
    void onInitializeResponse(const QJsonObject &result);
    void fetchAllFeatures();

    McpServerConfig m_config;
    State m_state = State::Disconnected;
    QString m_lastError;
    McpServerInfo m_serverInfo;

    QNetworkAccessManager *m_nam = nullptr;
    QProcess *m_process = nullptr;
    QNetworkReply *m_streamReply = nullptr; // legacy SSE event stream (GET)
    QByteArray m_stdioBuffer;
    QString m_sseSessionId;

    QHash<QString, PendingCall> m_pending;        // request id -> call
    QQueue<PendingCall> m_callQueue;              // calls waiting for the handshake
    QHash<QString, QJsonObject> m_queuedMessages; // request id -> unsent message
    QHash<QString, QString> m_toolCallIds;        // request id -> tool name
    QHash<QString, QString> m_pendingPromptNames; // request id -> prompt name
    bool m_initialized = false;
    bool m_handshakeDone = false;
};

} // namespace ocapi
