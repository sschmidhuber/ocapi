#pragma once

#include "core/backendconfig.h"
#include "core/message.h"

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ocapi {

class ChatSession;

class IBackend : public QObject {
    Q_OBJECT

public:
    explicit IBackend(BackendConfig config, QObject *parent = nullptr)
        : QObject(parent), m_config(std::move(config)) {}

    ~IBackend() override = default;

    [[nodiscard]] const BackendConfig &config() const { return m_config; }
    [[nodiscard]] BackendConfig &config() { return m_config; }

    virtual void listModels() = 0;
    virtual void sendMessage(ChatSession *session, const QString &model) = 0;
    virtual void cancel() = 0;

    // True when the backend/model supports function calling. When false the
    // UI will not offer MCP tools to the model.
    [[nodiscard]] virtual bool supportsTools() const { return false; }

    // Tools offered to the model on the next sendMessage() call. Only called
    // when supportsTools() is true.
    virtual void setAvailableTools(const QJsonArray &tools) { (void)tools; }

signals:
    void modelsReady(const QStringList &models);
    void modelsFailed(const QString &error);

    void tokenReceived(const QUuid &messageId, const QString &accumulatedText);
    void finished(const QUuid &messageId);
    void failed(const QUuid &messageId, const QString &error);
    // The model wants to invoke tools; the UI/agent layer executes them and
    // sends the results back with sendMessage().
    void toolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls);

protected:
    BackendConfig m_config;
};

} // namespace ocapi
