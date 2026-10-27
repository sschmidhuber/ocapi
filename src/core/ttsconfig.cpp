#include "core/ttsconfig.h"

#include <QHash>

namespace ocapi {

QString TtsServiceConfig::kindName(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return QStringLiteral("OpenAI");
    case TtsServiceKind::Groq:
        return QStringLiteral("Groq");
    case TtsServiceKind::Mistral:
        return QStringLiteral("Mistral AI");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("ElevenLabs");
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("OpenAI-compatible TTS server");
    case TtsServiceKind::Piper:
        return QStringLiteral("Piper HTTP server");
    }
    return QStringLiteral("Unknown");
}

QString TtsServiceConfig::kindKey(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return QStringLiteral("openai");
    case TtsServiceKind::Groq:
        return QStringLiteral("groq");
    case TtsServiceKind::Mistral:
        return QStringLiteral("mistral");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("elevenlabs");
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("openaitts");
    case TtsServiceKind::Piper:
        return QStringLiteral("piper");
    }
    return {};
}

TtsServiceKind TtsServiceConfig::kindFromString(const QString &key) {
    static const QHash<QString, TtsServiceKind> map{
        {QStringLiteral("openai"), TtsServiceKind::OpenAiApi},
        {QStringLiteral("groq"), TtsServiceKind::Groq},
        {QStringLiteral("mistral"), TtsServiceKind::Mistral},
        {QStringLiteral("elevenlabs"), TtsServiceKind::ElevenLabs},
        {QStringLiteral("openaitts"), TtsServiceKind::OpenAiTts},
        {QStringLiteral("piper"), TtsServiceKind::Piper},
    };
    return map.value(key, TtsServiceKind::OpenAiTts);
}

QString TtsServiceConfig::kindDescription(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return QStringLiteral("OpenAI speech API (tts-1, tts-1-hd, gpt-4o-mini-tts, \u2026)");
    case TtsServiceKind::Groq:
        return QStringLiteral("Groq TTS models (OpenAI-compatible speech API)");
    case TtsServiceKind::Mistral:
        return QStringLiteral("Mistral AI speech API (OpenAI-compatible)");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("ElevenLabs speech synthesis (v2.5/flash/turbo, multilingual)");
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("Any OpenAI-compatible TTS server, local or remote (e.g. "
                              "OpenAI-TTS-compatible gateways)");
    case TtsServiceKind::Piper:
        return QStringLiteral("Piper HTTP server (rhasspy/piper), local or remote");
    }
    return {};
}

QString TtsServiceConfig::defaultBaseUrl(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return QStringLiteral("https://api.openai.com/v1");
    case TtsServiceKind::Groq:
        return QStringLiteral("https://api.groq.com/openai/v1");
    case TtsServiceKind::Mistral:
        return QStringLiteral("https://api.mistral.ai/v1");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("https://api.elevenlabs.io/v1");
    case TtsServiceKind::OpenAiTts:
    case TtsServiceKind::Piper:
        return QStringLiteral("http://127.0.0.1:5000");
    }
    return {};
}

QString TtsServiceConfig::defaultModel(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return QStringLiteral("tts-1");
    case TtsServiceKind::Groq:
        return QStringLiteral("playai-tts");
    case TtsServiceKind::Mistral:
        return QStringLiteral("mistral-tts-latest");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("eleven_flash_v2_5");
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("tts-1");
    case TtsServiceKind::Piper:
        return QStringLiteral("de_DE-thorsten-medium");
    }
    return {};
}

QString TtsServiceConfig::defaultVoice(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
    case TtsServiceKind::Groq:
    case TtsServiceKind::Mistral:
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("alloy");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("21m00Tcm4TlvDq8ikWAM"); // "Rachel"
    case TtsServiceKind::Piper:
        return {};
    }
    return {};
}

bool TtsServiceConfig::isCloud(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
    case TtsServiceKind::Groq:
    case TtsServiceKind::Mistral:
    case TtsServiceKind::ElevenLabs:
        return true;
    case TtsServiceKind::OpenAiTts:
    case TtsServiceKind::Piper:
        return false;
    }
    return false;
}

bool TtsServiceConfig::requiresApiKey(TtsServiceKind kind) {
    return isCloud(kind);
}

const QVector<TtsServiceKind> &TtsServiceConfig::allKinds() {
    static const QVector<TtsServiceKind> kinds{
        TtsServiceKind::OpenAiApi,  TtsServiceKind::Groq,      TtsServiceKind::Mistral,
        TtsServiceKind::ElevenLabs, TtsServiceKind::OpenAiTts, TtsServiceKind::Piper,
    };
    return kinds;
}

std::optional<BackendKind> TtsServiceConfig::llmProviderKind(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
        return BackendKind::OpenAi;
    case TtsServiceKind::Groq:
        return BackendKind::Groq;
    case TtsServiceKind::Mistral:
        return BackendKind::Mistral;
    case TtsServiceKind::ElevenLabs:
    case TtsServiceKind::OpenAiTts:
    case TtsServiceKind::Piper:
        break;
    }
    return std::nullopt;
}

QString TtsServiceConfig::responseFormat(TtsServiceKind kind) {
    return kind == TtsServiceKind::Piper ? QStringLiteral("wav") : QStringLiteral("mp3");
}

QString TtsServiceConfig::responseMimeType(TtsServiceKind kind) {
    return kind == TtsServiceKind::Piper ? QStringLiteral("audio/wav")
                                         : QStringLiteral("audio/mpeg");
}

QString TtsServiceConfig::speechPath() const {
    switch (kind) {
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("/text-to-speech/%1").arg(effectiveVoice());
    case TtsServiceKind::Piper:
        return QStringLiteral("/api/tts");
    case TtsServiceKind::OpenAiApi:
    case TtsServiceKind::Groq:
    case TtsServiceKind::Mistral:
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("/audio/speech");
    }
    return QStringLiteral("/audio/speech");
}

QJsonObject TtsServiceConfig::toJson() const {
    return QJsonObject{
        {QStringLiteral("id"), id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), name},
        {QStringLiteral("kind"), kindKey(kind)},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("apiKey"), apiKey},
        {QStringLiteral("model"), model},
        {QStringLiteral("voice"), voice},
        {QStringLiteral("language"), language},
        {QStringLiteral("enabled"), enabled},
    };
}

TtsServiceConfig TtsServiceConfig::fromJson(const QJsonObject &object) {
    TtsServiceConfig config;
    config.id = QUuid(object.value(QStringLiteral("id")).toString());
    config.name = object.value(QStringLiteral("name")).toString();
    config.kind = kindFromString(object.value(QStringLiteral("kind")).toString());
    config.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    config.apiKey = object.value(QStringLiteral("apiKey")).toString();
    config.model = object.value(QStringLiteral("model")).toString();
    config.voice = object.value(QStringLiteral("voice")).toString();
    config.language = object.value(QStringLiteral("language")).toString();
    config.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    return config;
}

} // namespace ocapi
