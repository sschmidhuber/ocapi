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
