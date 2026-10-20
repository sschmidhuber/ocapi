#pragma once

#include <QString>
#include <QUuid>

namespace ocapi {

enum class BackendKind {
    Ollama,
    LlamaCpp,
    Vllm,
    OpenAi,
    Anthropic,
    Gemini,
    Mistral,
};

struct BackendConfig {
    QUuid id = QUuid::createUuid();
    QString name;
    BackendKind kind = BackendKind::Ollama;
    QString baseUrl = QStringLiteral("http://127.0.0.1:11434");
    QString apiKey;
    QString defaultModel;
    bool enabled = true;

    [[nodiscard]] static QString kindName(BackendKind kind);
    [[nodiscard]] static QString kindDescription(BackendKind kind);
    [[nodiscard]] static QString defaultBaseUrl(BackendKind kind);
    [[nodiscard]] QString kindName() const { return kindName(kind); }
};

} // namespace ocapi
