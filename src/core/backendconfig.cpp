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
    }
    return {};
}

} // namespace ocapi
