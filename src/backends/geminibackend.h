#pragma once

#include "backends/ibackend.h"

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

class SseClient;

class GeminiBackend : public IBackend {
    Q_OBJECT

public:
    explicit GeminiBackend(BackendConfig config, QObject *parent = nullptr);
    ~GeminiBackend() override;

    void listModels() override;
    void sendMessage(ChatSession *session, const QString &model) override;
    void cancel() override;

private:
    QNetworkAccessManager *m_nam;
    std::unique_ptr<SseClient> m_stream;
    QNetworkReply *m_modelsReply = nullptr;
    QUuid m_currentMessageId;
    QString m_accumulated;
    QString m_accumulatedReasoning;
};

} // namespace ocapi
