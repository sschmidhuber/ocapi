#pragma once

#include "core/backendconfig.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <optional>

namespace okapi {

enum class TtsServiceKind {
    OpenAiApi,  // OpenAI speech API
    Groq,       // Groq TTS (OpenAI-compatible)
    Mistral,    // Mistral AI speech API (OpenAI-compatible)
    ElevenLabs, // ElevenLabs speech synthesis
    OpenAiTts,  // any other OpenAI-compatible TTS server, local or remote
    Piper,      // Piper HTTP server (rhasspy/piper), local or remote
};

// Discrete speech paces. Mistral/Voxtral only accepts these five inline
// [pace=...] values, so the settings slider steps through them for that
// backend instead of offering a continuous speed factor.
enum class TtsPace { VerySlow, Slow, Moderate, Fast, VeryFast };

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
    double speed = 1.0; // playback speed factor, 0.25 .. 4.0
    QString language;   // ISO 639-1 code, optional
    bool enabled = true;

    [[nodiscard]] static QString kindName(TtsServiceKind kind);
    [[nodiscard]] static QString kindKey(TtsServiceKind kind);
    [[nodiscard]] static TtsServiceKind kindFromString(const QString &key);
    [[nodiscard]] static QString kindDescription(TtsServiceKind kind);
    [[nodiscard]] static QString defaultBaseUrl(TtsServiceKind kind);
    [[nodiscard]] static QString defaultModel(TtsServiceKind kind);
    [[nodiscard]] static QString defaultVoice(TtsServiceKind kind);
    // Preset voices offered in the settings page dropdown. Empty for
    // services where the voice is a free-form model or ID (Piper, Mistral,
    // ElevenLabs); the combo stays editable so custom values remain possible.
    [[nodiscard]] static QStringList availableVoices(TtsServiceKind kind);
    // Speech speed bounds accepted by the OpenAI-compatible APIs and the
    // speed slider in the settings page.
    static constexpr double minSpeed = 0.25;
    static constexpr double maxSpeed = 4.0;
    static constexpr double defaultSpeed = 1.0;
    // Whether the service's output speed is configurable from the settings
    // page. Mistral has no "speed" request field, but pace can be set via
    // inline [pace=...] tags in the input text, so it counts as supported.
    [[nodiscard]] static bool supportsSpeed(TtsServiceKind kind);
    // The discrete paces Mistral accepts as inline [pace=...] tags.
    [[nodiscard]] static QVector<TtsPace> availablePaces();
    // Inline Voxtral pace tag ("[pace=very-slow]" .. "[pace=very-fast]").
    [[nodiscard]] static QString mistralPaceTag(TtsPace pace);
    // The pace closest to a linear speed factor (1.0 = Moderate).
    [[nodiscard]] static TtsPace paceFromSpeed(double speed);
    // Representative linear speed factor of a pace, used to persist the
    // setting as "speed" in JSON for every backend.
    [[nodiscard]] static double speedFromPace(TtsPace pace);
    // Slider label for a speed value: the pace name for Mistral, a factor
    // ("1.50×") for the other backends.
    [[nodiscard]] static QString speedLabel(double speed, TtsServiceKind kind);
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
    // Speed clamped to [minSpeed, maxSpeed]; 1.0 when unset or invalid.
    [[nodiscard]] double effectiveSpeed() const {
        if (speed < minSpeed || speed > maxSpeed)
            return defaultSpeed;
        return speed;
    }

    [[nodiscard]] QJsonObject toJson() const;
    static TtsServiceConfig fromJson(const QJsonObject &object);
};

} // namespace okapi
