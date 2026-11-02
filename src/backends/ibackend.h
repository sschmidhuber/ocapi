#pragma once

#include "core/backendconfig.h"
#include "core/chatsession.h"
#include "core/message.h"
#include "core/tooldebug.h"

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
    // Sends the conversation to the model. When \p continueMessageId is not
    // null, the reply continues that existing assistant message (tool-call
    // rounds) instead of appending a new one, so one assistant turn stays a
    // single message in the chat.
    virtual void sendMessage(ChatSession *session, const QString &model,
                             const QUuid &continueMessageId = {}) = 0;
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
    // Reasoning ("thinking") tokens streamed before the visible answer;
    // \p accumulatedReasoning is the full reasoning text so far.
    void reasoningReceived(const QUuid &messageId, const QString &accumulatedReasoning);
    void finished(const QUuid &messageId);
    void failed(const QUuid &messageId, const QString &error);
    // The model wants to invoke tools; the UI/agent layer executes them and
    // sends the results back with sendMessage().
    void toolCallsRequested(const QUuid &messageId, const QVector<ToolCall> &toolCalls);

protected:
    // Starts a new streaming assistant message, or resumes \p continueMessageId
    // when it is valid. Seeds the accumulation buffers from the existing
    // message so streamed tokens append to what earlier rounds produced.
    static QUuid beginAssistantMessage(ChatSession *session, const QUuid &continueMessageId,
                                       QString &accumulated, QString &accumulatedReasoning) {
        if (!continueMessageId.isNull()) {
            for (const auto &message : session->messages()) {
                if (message.id == continueMessageId) {
                    accumulated = message.content;
                    // Each round starts a fresh thinking phase: the new
                    // reasoning becomes its own segment in the message.
                    accumulatedReasoning.clear();
                    session->updateMessage(continueMessageId, message.content, true);
                    return continueMessageId;
                }
            }
        }
        accumulated.clear();
        accumulatedReasoning.clear();
        return session->appendMessage(Role::Assistant, {}, true).id;
    }

    // With OCAPI_DEBUG_TOOLS set, dumps the exact messages array sent to the
    // model, so it is visible whether tool results are in the context.
    static void debugLogRequest(const QString &backendName, const QJsonArray &messages) {
        if (toolDebug::enabled()) {
            toolDebug::log(backendName + QStringLiteral(" request messages (%1): %2")
                                             .arg(static_cast<int>(messages.size()))
                                             .arg(QString::fromUtf8(QJsonDocument(messages).toJson(
                                                 QJsonDocument::Compact))));
        }
    }

    BackendConfig m_config;
};

} // namespace ocapi
