#pragma once
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

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
    // Whether MCP tools are offered to the model on every request. Some local
    // models (e.g. qwen3.5 via Ollama's native /api/chat) lose conversation
    // history recall when tool definitions are present, so this can be
    // disabled per backend.
    bool toolsEnabled = true;

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

} // namespace ocapi
