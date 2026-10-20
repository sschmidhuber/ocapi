#include "backends/backendfactory.h"

#include "backends/anthropicbackend.h"
#include "backends/geminibackend.h"
#include "backends/ibackend.h"
#include "backends/ollamabackend.h"
#include "backends/openaiapibackend.h"
#include "core/backendconfig.h"

namespace ocapi {

std::unique_ptr<IBackend> createBackend(const BackendConfig& config)
{
    switch (config.kind) {
    case BackendKind::Ollama:
        return std::make_unique<OllamaBackend>(config);
    case BackendKind::LlamaCpp:
    case BackendKind::Vllm:
    case BackendKind::OpenAi:
    case BackendKind::Mistral:
        return std::make_unique<OpenAiApiBackend>(config);
    case BackendKind::Anthropic:
        return std::make_unique<AnthropicBackend>(config);
    case BackendKind::Gemini:
        return std::make_unique<GeminiBackend>(config);
    }
    return nullptr;
}

} // namespace ocapi
