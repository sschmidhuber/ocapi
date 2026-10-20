#pragma once

#include "backends/ibackend.h"

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

class SseClient;

// OpenAI Chat Completions compatible backend. Covers llama.cpp, vLLM and
// OpenAI-compatible cloud endpoints. Constructed with a BackendKind so the
// default paths and headers can be adjusted per provider.
class OpenAiApiBackend : public IBackend {
    Q_OBJECT

public:
    explicit OpenAiApiBackend(BackendConfig config, QObject *parent = nullptr);
    ~OpenAiApiBackend() override;

    void listModels() override;
    void sendMessage(ChatSession *session, const QString &model) override;
    void cancel() override;

private:
    QNetworkAccessManager *m_nam;
    std::unique_ptr<SseClient> m_stream;
    QNetworkReply *m_modelsReply = nullptr;
    QUuid m_currentMessageId;
    QString m_accumulated;
};

} // namespace ocapi
