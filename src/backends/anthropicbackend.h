#pragma once

#include "backends/ibackend.h"

#include <memory>

#include <QHash>
#include <QJsonArray>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

class SseClient;

class AnthropicBackend : public IBackend {
    Q_OBJECT

public:
    explicit AnthropicBackend(BackendConfig config, QObject *parent = nullptr);
    ~AnthropicBackend() override;

    void listModels() override;
    void sendMessage(ChatSession *session, const QString &model,
                     const QUuid &continueMessageId = {}) override;
    void cancel() override;
    [[nodiscard]] bool supportsTools() const override { return true; }
    void setAvailableTools(const QJsonArray &tools) override;

private:
    void onStreamEvent(const QString &eventName, const QByteArray &data);
    void finishToolCallsIfComplete();

    QNetworkAccessManager *m_nam;
    std::unique_ptr<SseClient> m_stream;
    QNetworkReply *m_modelsReply = nullptr;
    QUuid m_currentMessageId;
    QString m_accumulated;
    QString m_accumulatedReasoning;
    QJsonArray m_availableTools;
    bool m_toolsEnabled = false;
    QVector<ToolCall> m_pendingToolCalls;
    QHash<int, QString> m_argumentsBuffer;
};

} // namespace ocapi
