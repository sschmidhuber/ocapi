#pragma once

#include "core/backendconfig.h"

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>
#include <optional>

namespace ocapi {

enum class TtsServiceKind {
    OpenAiApi,  // OpenAI speech API
    Groq,       // Groq TTS (OpenAI-compatible)
    Mistral,    // Mistral AI speech API (OpenAI-compatible)
    ElevenLabs, // ElevenLabs speech synthesis
    OpenAiTts,  // any other OpenAI-compatible TTS server, local or remote
    Piper,      // Piper HTTP server (rhasspy/piper), local or remote
};

// Configuration of one text-to-speech service, analogous to
// SttServiceConfig. TTS services are always request/response (no streaming
// audio), so the latency-critical path is the sentence splitter plus the
// queued playback in TtsPlayer.
struct TtsServiceConfig {
    QUuid id = QUuid::createUuid();
    QString name;
    TtsServiceKind kind = TtsServiceKind::OpenAiApi;
    QString baseUrl;
    QString apiKey;
    QString model;
    QString voice;
    QString language; // ISO 639-1 code, optional
    bool enabled = true;

    [[nodiscard]] static QString kindName(TtsServiceKind kind);
    [[nodiscard]] static QString kindKey(TtsServiceKind kind);
    [[nodiscard]] static TtsServiceKind kindFromString(const QString &key);
    [[nodiscard]] static QString kindDescription(TtsServiceKind kind);
    [[nodiscard]] static QString defaultBaseUrl(TtsServiceKind kind);
    [[nodiscard]] static QString defaultModel(TtsServiceKind kind);
    [[nodiscard]] static QString defaultVoice(TtsServiceKind kind);
    [[nodiscard]] static bool isCloud(TtsServiceKind kind);
    [[nodiscard]] static bool requiresApiKey(TtsServiceKind kind);
    [[nodiscard]] static const QVector<TtsServiceKind> &allKinds();
    // The LLM provider that shares this TTS provider's account/API key,
    // or std::nullopt when there is no corresponding LLM provider.
    [[nodiscard]] static std::optional<BackendKind> llmProviderKind(TtsServiceKind kind);
    // Audio format requested from the service. Piper serves WAV; everything
    // else gets MP3 (decoded by QAudioDecoder for playback).
    [[nodiscard]] static QString responseFormat(TtsServiceKind kind);
    // MIME type of the audio returned by responseFormat().
    [[nodiscard]] static QString responseMimeType(TtsServiceKind kind);
    // HTTP path of the synthesis endpoint, relative to the base URL.
    [[nodiscard]] QString speechPath() const;

    [[nodiscard]] bool isCloud() const { return isCloud(kind); }
    [[nodiscard]] QString kindName() const { return kindName(kind); }
    [[nodiscard]] QString effectiveBaseUrl() const {
        const QString trimmed = baseUrl.trimmed();
        return trimmed.isEmpty() ? defaultBaseUrl(kind) : trimmed;
    }
    [[nodiscard]] QString effectiveModel() const {
        const QString trimmed = model.trimmed();
        return trimmed.isEmpty() ? defaultModel(kind) : trimmed;
    }
    [[nodiscard]] QString effectiveVoice() const {
        const QString trimmed = voice.trimmed();
        return trimmed.isEmpty() ? defaultVoice(kind) : trimmed;
    }

    [[nodiscard]] QJsonObject toJson() const;
    static TtsServiceConfig fromJson(const QJsonObject &object);
};

} // namespace ocapi
