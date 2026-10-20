#pragma once

#include "backends/ibackend.h"

#include <QHash>
#include <QJsonArray>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

class SseClient;

// OpenAI Chat Completions compatible backend. Covers llama.cpp, vLLM and
// OpenAI-compatible cloud endpoints. Constructed with a BackendKind so the
// default paths and headers can be adjusted per provider. Supports function
// calling (tools) via setAvailableTools().
class OpenAiApiBackend : public IBackend {
    Q_OBJECT

public:
    explicit OpenAiApiBackend(BackendConfig config, QObject *parent = nullptr);
    ~OpenAiApiBackend() override;

    void listModels() override;
    void sendMessage(ChatSession *session, const QString &model) override;
    void cancel() override;
    [[nodiscard]] bool supportsTools() const override;
    void setAvailableTools(const QJsonArray &tools) override;

private:
    void onStreamEvent(const QByteArray &data);
    void processToolCallDeltas(const QJsonArray &toolCallDeltas);
    void finishIfToolCallsComplete();

    QNetworkAccessManager *m_nam;
    std::unique_ptr<SseClient> m_stream;
    QNetworkReply *m_modelsReply = nullptr;
    QUuid m_currentMessageId;
    QString m_accumulated;
    QJsonArray m_availableTools;
    bool m_toolsEnabled = false;
    QVector<ToolCall> m_pendingToolCalls;
    QHash<int, QString> m_argumentsBuffer; // tool call index -> accumulated arguments JSON
    bool m_hasToolCalls = false;
};

} // namespace ocapi
