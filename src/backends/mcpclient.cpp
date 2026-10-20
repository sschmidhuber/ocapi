#include "backends/mcpclient.h"

#include "backends/sseparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>

namespace ocapi {

namespace {

// JSON-RPC 2.0 error code.
constexpr int kMethodNotFound = -32601;

QNetworkRequest makeRequest(const QUrl &url, const QStringList &headers) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    for (const auto &header : headers) {
        const int colon = header.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        request.setRawHeader(header.left(colon).trimmed().toUtf8(),
                             header.mid(colon + 1).trimmed().toUtf8());
    }
    return request;
}

} // namespace

McpClient::McpClient(McpServerConfig config, QObject *parent)
    : QObject(parent), m_config(std::move(config)) {
    m_nam = new QNetworkAccessManager(this);
}

McpClient::~McpClient() {
    stopTransport();
}

void McpClient::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void McpClient::fail(const QString &error) {
    m_lastError = error;
    if (m_state != State::Failed) {
        stopTransport();
        setState(State::Failed);
    }
    emit connectionFailed(error);
}

void McpClient::connectToServer() {
    if (m_state == State::Connecting || m_state == State::Connected)
        return;
    m_lastError.clear();
    setState(State::Connecting);
    if (m_config.transport == McpTransport::Stdio)
        startStdio();
    else
        startHttp();
}

void McpClient::disconnectFromServer() {
    stopTransport();
    m_initialized = false;
    m_handshakeDone = false;
    setState(State::Disconnected);
}

void McpClient::stopTransport() {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_streamReply) {
        m_streamReply->disconnect(this);
        m_streamReply->abort();
        m_streamReply->deleteLater();
        m_streamReply = nullptr;
    }
    m_stdioBuffer.clear();
    m_sseSessionId.clear();
    const auto pending = m_pending;
    m_pending.clear();
    m_toolCallIds.clear();
    m_pendingPromptNames.clear();
    m_callQueue.clear();
    m_queuedMessages.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        emit requestFailed(it.value().method, tr("Disconnected"));
}

void McpClient::startStdio() {
    if (m_config.command.trimmed().isEmpty()) {
        fail(tr("No command configured for stdio server \"%1\".").arg(m_config.name));
        return;
    }
    m_process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto &entry : m_config.environment) {
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals > 0)
            environment.insert(entry.left(equals), entry.mid(equals + 1));
    }
    m_process->setProcessEnvironment(environment);
    m_process->setProgram(m_config.command);
    m_process->setArguments(m_config.arguments);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &McpClient::onProcessReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &McpClient::onProcessErrorOccurred);
    connect(m_process, &QProcess::finished, this, &McpClient::onProcessFinished);
    m_process->start();
    // errorOccurred(FailedToStart) fires asynchronously when the binary is
    // missing; onProcessErrorOccurred reports the failure.
    if (m_process->state() == QProcess::NotRunning && m_process->error() == QProcess::FailedToStart)
        fail(tr("Failed to start MCP server process \"%1\".").arg(m_config.command));
}

void McpClient::startHttp() {
    if (m_config.url.trimmed().isEmpty()) {
        fail(tr("No URL configured for MCP server \"%1\".").arg(m_config.name));
        return;
    }
    if (m_config.transport == McpTransport::HttpSse) {
        // Legacy transport: open the GET event stream first, then initialize.
        QNetworkRequest request = makeRequest(QUrl(m_config.url), m_config.headers);
        request.setRawHeader("Accept", "text/event-stream");
        m_streamReply = m_nam->get(request);
        connect(m_streamReply, &QNetworkReply::readyRead, this, [this]() {
            m_stdioBuffer.append(m_streamReply->readAll());
            for (const auto &rawEvent : extractCompleteEvents(m_stdioBuffer)) {
                const auto parsed = parseSseEventBlock(rawEvent);
                if (parsed)
                    onSseEvent(parsed->event, parsed->data);
            }
        });
        connect(m_streamReply, &QNetworkReply::finished, this, [this]() {
            if (m_state == State::Connecting || m_state == State::Connected) {
                m_stdioBuffer.clear();
                fail(tr("Event stream for MCP server \"%1\" closed: %2")
                         .arg(m_config.name, m_streamReply->errorString()));
            }
        });
    }
    // Kick off the initialize handshake; the HTTP POST path is used for it.
    sendRequest(QStringLiteral("initialize"), initializeParamsJson());
}

void McpClient::onProcessReadyRead() {
    if (!m_process)
        return;
    m_stdioBuffer.append(m_process->readAllStandardOutput());
    while (true) {
        const int newline = m_stdioBuffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_stdioBuffer.left(newline).trimmed();
        m_stdioBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            handleIncoming(line);
    }
}

void McpClient::onProcessErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart && m_state == State::Connecting)
        fail(tr("Failed to start MCP server process \"%1\": %2")
                 .arg(m_config.command, m_process ? m_process->errorString() : QString()));
}

void McpClient::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
    if (m_state == State::Connecting || m_state == State::Connected)
        fail(tr("MCP server process \"%1\" exited unexpectedly.").arg(m_config.command));
}

QJsonValue McpClient::sendRequest(const QString &method, const QJsonObject &params) {
    const QJsonValue id = nextRequestId();
    const QJsonObject message = jsonRpcRequest(method, params, id);
    if (m_initialized || method == QLatin1String("initialize")) {
        sendMessage(message);
        m_pending.insert(id.toString(), PendingCall{method, id.toString()});
    } else {
        // Queue calls made before the handshake completes.
        m_callQueue.enqueue(PendingCall{method, id.toString()});
        m_pending.insert(id.toString(), PendingCall{method, id.toString()});
        // The message itself is sent once initialization finished; keep the
        // payload around by stashing it on the pending entry.
        m_pending[id.toString()].method = method;
        m_queuedMessages.insert(id.toString(), message);
    }
    return id;
}

void McpClient::sendNotification(const QString &method, const QJsonObject &params) {
    if (!m_initialized)
        return;
    sendMessage(jsonRpcNotification(method, params));
}

void McpClient::sendMessage(const QJsonObject &message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (m_process) {
        m_process->write(payload);
        m_process->write("\n");
        m_process->waitForBytesWritten(1000);
    } else if (m_config.transport != McpTransport::Stdio) {
        sendHttpPost(message);
    }
}

void McpClient::sendHttpPost(const QJsonObject &message) {
    QNetworkRequest request = makeRequest(QUrl(m_config.url), m_config.headers);
    if (!m_sseSessionId.isEmpty())
        request.setRawHeader("Mcp-Session-Id", m_sseSessionId.toUtf8());
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_nam->post(request, payload);
    const bool isRequest = message.contains(QStringLiteral("id"));
    const QString requestId =
        isRequest ? message.value(QStringLiteral("id")).toString() : QString();
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId]() { onPostFinished(reply, requestId); });
}

void McpClient::onPostFinished(QNetworkReply *reply, const QString &requestId) {
    reply->deleteLater();
    const QVariant sessionHeader = reply->rawHeader("Mcp-Session-Id");
    if (sessionHeader.isValid())
        m_sseSessionId = sessionHeader.toString();

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    if (reply->error() != QNetworkReply::NoError &&
        reply->error() != QNetworkReply::OperationCanceledError) {
        if (m_state == State::Connecting) {
            fail(tr("MCP server \"%1\" unreachable: %2").arg(m_config.name, reply->errorString()));
            return;
        }
        if (!requestId.isEmpty() && m_pending.contains(requestId)) {
            const QString method = m_pending.take(requestId).method;
            emit requestFailed(method, reply->errorString());
        }
        return;
    }
    // 202 Accepted: notification or response delivered asynchronously (SSE).
    if (status == 202)
        return;

    if (contentType.startsWith(QLatin1String("text/event-stream"))) {
        QByteArray buffer = body;
        for (const auto &rawEvent : extractCompleteEvents(buffer)) {
            const auto parsed = parseSseEventBlock(rawEvent);
            if (parsed)
                onSseEvent(parsed->event, parsed->data);
        }
        return;
    }

    // Plain JSON response (Streamable HTTP) or a JSON-RPC error body.
    const auto doc = QJsonDocument::fromJson(body);
    if (doc.isArray()) {
        for (const auto &value : doc.array())
            handleJsonRpcMessage(value.toObject());
        return;
    }
    if (doc.isObject()) {
        handleJsonRpcMessage(doc.object());
        return;
    }
    if (status >= 400) {
        if (!requestId.isEmpty() && m_pending.contains(requestId)) {
            const QString method = m_pending.take(requestId).method;
            emit requestFailed(method, tr("HTTP %1: %2").arg(status).arg(QString::fromUtf8(body)));
        } else if (m_state == State::Connecting) {
            fail(tr("MCP server \"%1\" returned HTTP %2.").arg(m_config.name).arg(status));
        }
    }
}

void McpClient::onSseEvent(const QByteArray &event, const QByteArray &data) {
    Q_UNUSED(event)
    if (data.isEmpty())
        return;
    const auto message = QJsonDocument::fromJson(data).object();
    if (!message.isEmpty())
        handleJsonRpcMessage(message);
}

void McpClient::handleIncoming(const QByteArray &payload) {
    const auto message = QJsonDocument::fromJson(payload).object();
    if (!message.isEmpty())
        handleJsonRpcMessage(message);
}

void McpClient::handleJsonRpcMessage(const QJsonObject &message) {
    if (message.contains(QStringLiteral("id")) &&
        (message.contains(QStringLiteral("result")) || isJsonRpcError(message))) {
        handleResponse(message);
        return;
    }
    const QString method = jsonRpcMethodFromMessage(message);
    const QJsonObject params = jsonRpcParamsFromMessage(message);
    if (message.contains(QStringLiteral("id"))) {
        handleServerRequest(message.value(QStringLiteral("id")), method, params);
        return;
    }
    // Notification from the server.
    if (method == QLatin1String("notifications/message")) {
        const QString level = params.value(QStringLiteral("level")).toString();
        const QString logger = params.value(QStringLiteral("logger")).toString();
        const QJsonValue data = params.value(QStringLiteral("data"));
        const QString text =
            data.isString()
                ? data.toString()
                : QString::fromUtf8(QJsonDocument(data.toObject()).toJson(QJsonDocument::Compact));
        emit logMessage(level, logger, text);
        return;
    }
    if (method.startsWith(QLatin1String("notifications/")))
        emit serverNotification(method, params);
}

void McpClient::handleResponse(const QJsonObject &message) {
    const QString requestId = jsonRpcIdFromMessage(message).toString();
    const auto it = m_pending.constFind(requestId);
    if (it == m_pending.constEnd())
        return;
    const QString method = it->method;
    m_pending.erase(it);
    m_toolCallIds.remove(requestId);

    if (isJsonRpcError(message)) {
        emit requestFailed(method, jsonRpcErrorText(message));
        return;
    }
    const QJsonObject result = message.value(QStringLiteral("result")).toObject();

    if (method == QLatin1String("initialize")) {
        onInitializeResponse(result);
        return;
    }
    if (method == QLatin1String("tools/list")) {
        QVector<McpTool> tools;
        const auto array = result.value(QStringLiteral("tools")).toArray();
        tools.reserve(array.size());
        for (const auto &value : array)
            tools.push_back(
                toolFromJson(value.toObject(), m_config.id.toString(QUuid::WithoutBraces)));
        emit toolsReady(tools);
        return;
    }
    if (method == QLatin1String("resources/list")) {
        QVector<McpResource> resources;
        const auto array = result.value(QStringLiteral("resources")).toArray();
        resources.reserve(array.size());
        for (const auto &value : array)
            resources.push_back(
                resourceFromJson(value.toObject(), m_config.id.toString(QUuid::WithoutBraces)));
        emit resourcesReady(resources);
        return;
    }
    if (method == QLatin1String("resources/templates/list")) {
        QVector<McpResourceTemplate> templates;
        const auto array = result.value(QStringLiteral("resourceTemplates")).toArray();
        templates.reserve(array.size());
        for (const auto &value : array)
            templates.push_back(resourceTemplateFromJson(
                value.toObject(), m_config.id.toString(QUuid::WithoutBraces)));
        emit resourceTemplatesReady(templates);
        return;
    }
    if (method == QLatin1String("prompts/list")) {
        QVector<McpPrompt> prompts;
        const auto array = result.value(QStringLiteral("prompts")).toArray();
        prompts.reserve(array.size());
        for (const auto &value : array)
            prompts.push_back(
                promptFromJson(value.toObject(), m_config.id.toString(QUuid::WithoutBraces)));
        emit promptsReady(prompts);
        return;
    }
    if (method == QLatin1String("resources/read")) {
        const auto contents = result.value(QStringLiteral("contents")).toArray();
        if (!contents.isEmpty()) {
            const auto first = contents.first().toObject();
            emit resourceRead(first.value(QStringLiteral("uri")).toString(),
                              first.value(QStringLiteral("mimeType")).toString(),
                              first.value(QStringLiteral("text")).toString());
        }
        return;
    }
    if (method == QLatin1String("prompts/get")) {
        McpPromptContent content;
        const auto messages = result.value(QStringLiteral("messages")).toArray();
        content.messages.reserve(messages.size());
        for (const auto &value : messages) {
            const auto item = value.toObject();
            McpPromptMessage entry;
            entry.role = item.value(QStringLiteral("role")).toString();
            const auto itemContent = item.value(QStringLiteral("content")).toObject();
            entry.text = itemContent.value(QStringLiteral("text")).toString();
            content.messages.push_back(std::move(entry));
        }
        // The prompt name is not part of the result object; use the pending
        // call's params via the description field fallback.
        emit promptReady(m_pendingPromptNames.take(requestId), content);
        return;
    }
    const auto toolIt = m_toolCallIds.constFind(requestId);
    // tools/call responses carry the tool name in m_toolCallIds.
    if (toolIt != m_toolCallIds.constEnd()) {
        emit toolResult(toolIt.value(), toolResultFromJson(result));
        return;
    }
    if (method == QLatin1String("tools/call")) {
        emit toolResult(QString(), toolResultFromJson(result));
        return;
    }
}

void McpClient::handleServerRequest(const QJsonValue &id, const QString &method,
                                    const QJsonObject &params) {
    // ocapi does not provide LLM sampling to servers; answer with the standard
    // capability error so servers can fall back gracefully.
    if (method == QLatin1String("sampling/createMessage")) {
        respondError(id, kMethodNotFound,
                     QStringLiteral("ocapi does not support server-initiated sampling."));
        return;
    }
    if (method == QLatin1String("roots/list")) {
        respondResult(id, QJsonObject{{QStringLiteral("roots"), QJsonArray{}}});
        return;
    }
    if (method == QLatin1String("elicitation/create")) {
        respondError(id, kMethodNotFound,
                     QStringLiteral("ocapi does not answer server elicitation requests."));
        return;
    }
    emit serverRequest(id, method, params);
    respondError(id, kMethodNotFound, QStringLiteral("Unsupported server request: %1").arg(method));
}

void McpClient::respondError(const QJsonValue &id, int code, const QString &text) {
    sendMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"),
         QJsonObject{
             {QStringLiteral("code"), code},
             {QStringLiteral("message"), text},
         }},
    });
}

void McpClient::respondResult(const QJsonValue &id, const QJsonObject &result) {
    sendMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    });
}

void McpClient::onInitializeResponse(const QJsonObject &result) {
    m_serverInfo = serverInfoFromJson(result);
    m_initialized = true;
    // Notify the server that initialization is complete.
    sendNotification(QStringLiteral("notifications/initialized"), QJsonObject{});
    setState(State::Connected);
    emit connected(m_serverInfo);
    // Flush queued calls and fetch the feature catalog.
    while (!m_callQueue.isEmpty()) {
        const PendingCall call = m_callQueue.dequeue();
        const auto it = m_queuedMessages.constFind(call.requestId);
        if (it != m_queuedMessages.constEnd()) {
            sendMessage(it.value());
            m_queuedMessages.erase(it);
        }
    }
    fetchAllFeatures();
}

void McpClient::fetchAllFeatures() {
    if (!m_handshakeDone) {
        m_handshakeDone = true;
        listTools();
        listResources();
        listResourceTemplates();
        listPrompts();
    }
}

void McpClient::listTools() {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("tools/list"), tr("Not connected"));
        return;
    }
    sendRequest(QStringLiteral("tools/list"), QJsonObject{});
}

void McpClient::listResources() {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("resources/list"), tr("Not connected"));
        return;
    }
    sendRequest(QStringLiteral("resources/list"), QJsonObject{});
}

void McpClient::listResourceTemplates() {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("resources/templates/list"), tr("Not connected"));
        return;
    }
    sendRequest(QStringLiteral("resources/templates/list"), QJsonObject{});
}

void McpClient::listPrompts() {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("prompts/list"), tr("Not connected"));
        return;
    }
    sendRequest(QStringLiteral("prompts/list"), QJsonObject{});
}

void McpClient::readResource(const QString &uri) {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("resources/read"), tr("Not connected"));
        return;
    }
    sendRequest(QStringLiteral("resources/read"), QJsonObject{{QStringLiteral("uri"), uri}});
}

void McpClient::getPrompt(const QString &name, const QJsonObject &arguments) {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("prompts/get"), tr("Not connected"));
        return;
    }
    const QJsonValue id =
        sendRequest(QStringLiteral("prompts/get"), QJsonObject{
                                                       {QStringLiteral("name"), name},
                                                       {QStringLiteral("arguments"), arguments},
                                                   });
    m_pendingPromptNames.insert(id.toString(), name);
}

void McpClient::callTool(const QString &name, const QJsonObject &arguments) {
    if (!isConnected()) {
        emit requestFailed(QStringLiteral("tools/call"), tr("Not connected"));
        return;
    }
    const QJsonValue id =
        sendRequest(QStringLiteral("tools/call"), toolCallToJson(name, arguments));
    m_toolCallIds.insert(id.toString(), name);
}

void McpClient::cancelPending() {
    const auto pending = m_pending;
    m_pending.clear();
    m_toolCallIds.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        emit requestFailed(it.value().method, tr("Cancelled"));
}

} // namespace ocapi
