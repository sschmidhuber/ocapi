#include "core/backendconfig.h"

#include <QHash>

namespace ocapi {

QString BackendConfig::kindName(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
        return QStringLiteral("Ollama");
    case BackendKind::LlamaCpp:
        return QStringLiteral("llama.cpp");
    case BackendKind::Vllm:
        return QStringLiteral("vLLM");
    case BackendKind::OpenAi:
        return QStringLiteral("OpenAI");
    case BackendKind::Anthropic:
        return QStringLiteral("Anthropic");
    case BackendKind::Gemini:
        return QStringLiteral("Google Gemini");
    case BackendKind::Mistral:
        return QStringLiteral("Mistral AI");
    case BackendKind::Zai:
        return QStringLiteral("Z.ai (GLM)");
    case BackendKind::Qwen:
        return QStringLiteral("Alibaba Qwen");
    case BackendKind::DeepSeek:
        return QStringLiteral("DeepSeek");
    case BackendKind::Groq:
        return QStringLiteral("Groq");
    case BackendKind::OpenRouter:
        return QStringLiteral("OpenRouter");
    }
    return QStringLiteral("Unknown");
}

QString BackendConfig::kindKey(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
        return QStringLiteral("ollama");
    case BackendKind::LlamaCpp:
        return QStringLiteral("llamacpp");
    case BackendKind::Vllm:
        return QStringLiteral("vllm");
    case BackendKind::OpenAi:
        return QStringLiteral("openai");
    case BackendKind::Anthropic:
        return QStringLiteral("anthropic");
    case BackendKind::Gemini:
        return QStringLiteral("gemini");
    case BackendKind::Mistral:
        return QStringLiteral("mistral");
    case BackendKind::Zai:
        return QStringLiteral("zai");
    case BackendKind::Qwen:
        return QStringLiteral("qwen");
    case BackendKind::DeepSeek:
        return QStringLiteral("deepseek");
    case BackendKind::Groq:
        return QStringLiteral("groq");
    case BackendKind::OpenRouter:
        return QStringLiteral("openrouter");
    }
    return {};
}

BackendKind BackendConfig::kindFromString(const QString &key) {
    static const QHash<QString, BackendKind> map{
        {QStringLiteral("ollama"), BackendKind::Ollama},
        {QStringLiteral("llamacpp"), BackendKind::LlamaCpp},
        {QStringLiteral("vllm"), BackendKind::Vllm},
        {QStringLiteral("openai"), BackendKind::OpenAi},
        {QStringLiteral("anthropic"), BackendKind::Anthropic},
        {QStringLiteral("gemini"), BackendKind::Gemini},
        {QStringLiteral("mistral"), BackendKind::Mistral},
        {QStringLiteral("zai"), BackendKind::Zai},
        {QStringLiteral("qwen"), BackendKind::Qwen},
        {QStringLiteral("deepseek"), BackendKind::DeepSeek},
        {QStringLiteral("groq"), BackendKind::Groq},
        {QStringLiteral("openrouter"), BackendKind::OpenRouter},
    };
    return map.value(key, BackendKind::Ollama);
}

QString BackendConfig::kindDescription(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
        return QStringLiteral("Local or remote Ollama server (recommended for local models)");
    case BackendKind::LlamaCpp:
        return QStringLiteral("llama.cpp server (llama-server), local or remote");
    case BackendKind::Vllm:
        return QStringLiteral("vLLM OpenAI-compatible server, local or remote");
    case BackendKind::OpenAi:
        return QStringLiteral("OpenAI API or any OpenAI-compatible endpoint");
    case BackendKind::Anthropic:
        return QStringLiteral("Anthropic Messages API (Claude)");
    case BackendKind::Gemini:
        return QStringLiteral("Google Gemini API");
    case BackendKind::Mistral:
        return QStringLiteral("Mistral AI API (OpenAI-compatible)");
    case BackendKind::Zai:
        return QStringLiteral("Z.ai API with GLM models (OpenAI-compatible)");
    case BackendKind::Qwen:
        return QStringLiteral("Alibaba Cloud Model Studio / DashScope API (OpenAI-compatible)");
    case BackendKind::DeepSeek:
        return QStringLiteral("DeepSeek API (OpenAI-compatible)");
    case BackendKind::Groq:
        return QStringLiteral("Groq API (OpenAI-compatible)");
    case BackendKind::OpenRouter:
        return QStringLiteral("OpenRouter API (OpenAI-compatible access to many models)");
    }
    return {};
}

QString BackendConfig::defaultBaseUrl(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
        return QStringLiteral("http://127.0.0.1:11434");
    case BackendKind::LlamaCpp:
        return QStringLiteral("http://127.0.0.1:8080");
    case BackendKind::Vllm:
        return QStringLiteral("http://127.0.0.1:8000/v1");
    case BackendKind::OpenAi:
        return QStringLiteral("https://api.openai.com/v1");
    case BackendKind::Anthropic:
        return QStringLiteral("https://api.anthropic.com/v1");
    case BackendKind::Gemini:
        return QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
    case BackendKind::Mistral:
        return QStringLiteral("https://api.mistral.ai/v1");
    case BackendKind::Zai:
        return QStringLiteral("https://api.z.ai/api/paas/v4");
    case BackendKind::Qwen:
        return QStringLiteral("https://dashscope-intl.aliyuncs.com/compatible-mode/v1");
    case BackendKind::DeepSeek:
        return QStringLiteral("https://api.deepseek.com/v1");
    case BackendKind::Groq:
        return QStringLiteral("https://api.groq.com/openai/v1");
    case BackendKind::OpenRouter:
        return QStringLiteral("https://openrouter.ai/api/v1");
    }
    return {};
}

bool BackendConfig::isCloud(BackendKind kind) {
    switch (kind) {
    case BackendKind::Ollama:
    case BackendKind::LlamaCpp:
    case BackendKind::Vllm:
        return false;
    case BackendKind::OpenAi:
    case BackendKind::Anthropic:
    case BackendKind::Gemini:
    case BackendKind::Mistral:
    case BackendKind::Zai:
    case BackendKind::Qwen:
    case BackendKind::DeepSeek:
    case BackendKind::Groq:
    case BackendKind::OpenRouter:
        return true;
    }
    return false;
}

bool BackendConfig::requiresApiKey(BackendKind kind) {
    return isCloud(kind);
}

const QVector<BackendKind> &BackendConfig::allKinds() {
    static const QVector<BackendKind> kinds{
        BackendKind::Ollama,   BackendKind::LlamaCpp,  BackendKind::Vllm,
        BackendKind::OpenAi,   BackendKind::Anthropic, BackendKind::Gemini,
        BackendKind::Mistral,  BackendKind::Zai,       BackendKind::Qwen,
        BackendKind::DeepSeek, BackendKind::Groq,      BackendKind::OpenRouter,
    };
    return kinds;
}

} // namespace ocapi
