#pragma once

#include "backends/ibackend.h"

#include <memory>

class QNetworkAccessManager;

namespace ocapi {

class SseClient;

class AnthropicBackend : public IBackend {
    Q_OBJECT

public:
    explicit AnthropicBackend(BackendConfig config, QObject *parent = nullptr);
    ~AnthropicBackend() override;

    void listModels() override;
    void sendMessage(ChatSession *session, const QString &model) override;
    void cancel() override;

private:
    QNetworkAccessManager *m_nam;
    std::unique_ptr<SseClient> m_stream;
    QUuid m_currentMessageId;
    QString m_accumulated;
};

} // namespace ocapi
