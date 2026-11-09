#pragma once
#include <QString>
#include <QUuid>
#include <QVector>

namespace okapi {

enum class BackendKind {
    Ollama,
    LlamaCpp,
    Vllm,
    OpenAi,
    Anthropic,
    Gemini,
    Mistral,
    Zai,
    Qwen,
    DeepSeek,
    Groq,
    OpenRouter,
};

struct BackendConfig {
    QUuid id = QUuid::createUuid();
    QString name;
    BackendKind kind = BackendKind::Ollama;
    QString baseUrl;
    QString apiKey;
    QString defaultModel;
    bool enabled = true;
    // Context window (Ollama's num_ctx) requested from the server. Ollama
    // silently drops the oldest messages of a conversation when the rendered
    // prompt (system prompt + history + tool definitions) exceeds the window,
    // so a chat client must request a larger one than the server default.
    // 0 keeps the server's default; ignored by backends without the concept.
    int contextWindow = 8192;

    [[nodiscard]] static QString kindName(BackendKind kind);
    [[nodiscard]] static QString kindKey(BackendKind kind);
    [[nodiscard]] static BackendKind kindFromString(const QString &key);
    [[nodiscard]] static QString kindDescription(BackendKind kind);
    [[nodiscard]] static QString defaultBaseUrl(BackendKind kind);
    [[nodiscard]] static bool isCloud(BackendKind kind);
    [[nodiscard]] static bool requiresApiKey(BackendKind kind);
    [[nodiscard]] static const QVector<BackendKind> &allKinds();
    [[nodiscard]] bool isCloud() const { return isCloud(kind); }
    [[nodiscard]] QString effectiveBaseUrl() const {
        const QString trimmed = baseUrl.trimmed();
        return trimmed.isEmpty() ? defaultBaseUrl(kind) : trimmed;
    }
    [[nodiscard]] QString kindName() const { return kindName(kind); }
};

} // namespace okapi
