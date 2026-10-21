#include "core/sttconfig.h"

#include <QHash>

#include <algorithm>

namespace ocapi {

QString SttServiceConfig::kindName(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
        return QStringLiteral("OpenAI");
    case SttServiceKind::Groq:
        return QStringLiteral("Groq");
    case SttServiceKind::Mistral:
        return QStringLiteral("Mistral AI");
    case SttServiceKind::WhisperApi:
        return QStringLiteral("Whisper server (OpenAI-compatible)");
    case SttServiceKind::WhisperCpp:
        return QStringLiteral("whisper.cpp server");
    }
    return QStringLiteral("Unknown");
}

QString SttServiceConfig::kindKey(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
        return QStringLiteral("openai");
    case SttServiceKind::Groq:
        return QStringLiteral("groq");
    case SttServiceKind::Mistral:
        return QStringLiteral("mistral");
    case SttServiceKind::WhisperApi:
        return QStringLiteral("whisperapi");
    case SttServiceKind::WhisperCpp:
        return QStringLiteral("whispercpp");
    }
    return {};
}

SttServiceKind SttServiceConfig::kindFromString(const QString &key) {
    static const QHash<QString, SttServiceKind> map{
        {QStringLiteral("openai"), SttServiceKind::OpenAiApi},
        {QStringLiteral("groq"), SttServiceKind::Groq},
        {QStringLiteral("mistral"), SttServiceKind::Mistral},
        {QStringLiteral("whisperapi"), SttServiceKind::WhisperApi},
        {QStringLiteral("whispercpp"), SttServiceKind::WhisperCpp},
    };
    return map.value(key, SttServiceKind::WhisperApi);
}

QString SttServiceConfig::kindDescription(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
        return QStringLiteral("OpenAI transcription API (whisper-1, gpt-4o-transcribe, …)");
    case SttServiceKind::Groq:
        return QStringLiteral("Groq whisper models (OpenAI-compatible transcription API)");
    case SttServiceKind::Mistral:
        return QStringLiteral("Mistral AI audio transcription API");
    case SttServiceKind::WhisperApi:
        return QStringLiteral("Any OpenAI-compatible transcription server, local or remote (e.g. "
                              "faster-whisper-server, Speaches, LocalAI)");
    case SttServiceKind::WhisperCpp:
        return QStringLiteral("whisper.cpp server (whisper-server), local or remote");
    }
    return {};
}

QString SttServiceConfig::defaultBaseUrl(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
        return QStringLiteral("https://api.openai.com/v1");
    case SttServiceKind::Groq:
        return QStringLiteral("https://api.groq.com/openai/v1");
    case SttServiceKind::Mistral:
        return QStringLiteral("https://api.mistral.ai/v1");
    case SttServiceKind::WhisperApi:
        return QStringLiteral("http://127.0.0.1:8000/v1");
    case SttServiceKind::WhisperCpp:
        return QStringLiteral("http://127.0.0.1:8080");
    }
    return {};
}

QString SttServiceConfig::defaultModel(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
        return QStringLiteral("whisper-1");
    case SttServiceKind::Groq:
        return QStringLiteral("whisper-large-v3");
    case SttServiceKind::Mistral:
        return QStringLiteral("mistral-large-latest");
    case SttServiceKind::WhisperApi:
    case SttServiceKind::WhisperCpp:
        return QStringLiteral("whisper-1");
    }
    return {};
}

bool SttServiceConfig::isCloud(SttServiceKind kind) {
    switch (kind) {
    case SttServiceKind::OpenAiApi:
    case SttServiceKind::Groq:
    case SttServiceKind::Mistral:
        return true;
    case SttServiceKind::WhisperApi:
    case SttServiceKind::WhisperCpp:
        return false;
    }
    return false;
}

bool SttServiceConfig::requiresApiKey(SttServiceKind kind) {
    return isCloud(kind);
}

int SttServiceConfig::clampChunkDurationMs(int ms) {
    return std::clamp(ms, 1000, 10000);
}

const QVector<SttServiceKind> &SttServiceConfig::allKinds() {
    static const QVector<SttServiceKind> kinds{
        SttServiceKind::OpenAiApi,  SttServiceKind::Groq,       SttServiceKind::Mistral,
        SttServiceKind::WhisperApi, SttServiceKind::WhisperCpp,
    };
    return kinds;
}

QJsonObject SttServiceConfig::toJson() const {
    return QJsonObject{
        {QStringLiteral("id"), id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), name},
        {QStringLiteral("kind"), kindKey(kind)},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("apiKey"), apiKey},
        {QStringLiteral("model"), model},
        {QStringLiteral("language"), language},
        {QStringLiteral("chunkDurationMs"), chunkDurationMs},
        {QStringLiteral("enabled"), enabled},
    };
}

SttServiceConfig SttServiceConfig::fromJson(const QJsonObject &object) {
    SttServiceConfig config;
    config.id = QUuid(object.value(QStringLiteral("id")).toString());
    config.name = object.value(QStringLiteral("name")).toString();
    config.kind = kindFromString(object.value(QStringLiteral("kind")).toString());
    config.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    config.apiKey = object.value(QStringLiteral("apiKey")).toString();
    config.model = object.value(QStringLiteral("model")).toString();
    config.language = object.value(QStringLiteral("language")).toString();
    config.chunkDurationMs =
        clampChunkDurationMs(object.value(QStringLiteral("chunkDurationMs")).toInt(2500));
    config.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    return config;
}

} // namespace ocapi
