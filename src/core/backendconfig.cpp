#include "core/backendconfig.h"

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
    }
    return QStringLiteral("Unknown");
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
        return true;
    }
    return false;
}

bool BackendConfig::requiresApiKey(BackendKind kind) {
    return isCloud(kind);
}

const QVector<BackendKind> &BackendConfig::allKinds() {
    static const QVector<BackendKind> kinds{
        BackendKind::Ollama,    BackendKind::LlamaCpp, BackendKind::Vllm,    BackendKind::OpenAi,
        BackendKind::Anthropic, BackendKind::Gemini,   BackendKind::Mistral, BackendKind::Zai,
        BackendKind::Qwen,      BackendKind::DeepSeek, BackendKind::Groq,
    };
    return kinds;
}

} // namespace ocapi
