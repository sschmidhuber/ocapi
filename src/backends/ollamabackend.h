#pragma once

#include "backends/ibackend.h"

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace ocapi {

class SseClient;

class OllamaBackend : public IBackend {
    Q_OBJECT

public:
    explicit OllamaBackend(BackendConfig config, QObject *parent = nullptr);
    ~OllamaBackend() override;

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
