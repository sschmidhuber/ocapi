#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

enum class SttServiceKind {
    OpenAiApi,  // OpenAI transcription API
    Groq,       // Groq whisper models (OpenAI-compatible)
    Mistral,    // Mistral audio transcription (OpenAI-compatible)
    WhisperApi, // any other OpenAI-compatible server, local or remote
    WhisperCpp, // whisper.cpp server, local or remote
};

// Configuration of one speech-to-text service, analogous to BackendConfig.
struct SttServiceConfig {
    QUuid id = QUuid::createUuid();
    QString name;
    SttServiceKind kind = SttServiceKind::OpenAiApi;
    QString baseUrl;
    QString apiKey;
    QString model;
    QString language;           // ISO 639-1 code, optional
    int chunkDurationMs = 2500; // live-transcription latency per request
    bool enabled = true;

    [[nodiscard]] static QString kindName(SttServiceKind kind);
    [[nodiscard]] static QString kindKey(SttServiceKind kind);
    [[nodiscard]] static SttServiceKind kindFromString(const QString &key);
    [[nodiscard]] static QString kindDescription(SttServiceKind kind);
    [[nodiscard]] static QString defaultBaseUrl(SttServiceKind kind);
    [[nodiscard]] static QString defaultModel(SttServiceKind kind);
    [[nodiscard]] static bool isCloud(SttServiceKind kind);
    [[nodiscard]] static bool requiresApiKey(SttServiceKind kind);
    [[nodiscard]] static const QVector<SttServiceKind> &allKinds();
    // Clamps a live-chunk duration to the supported range (1-10 s).
    [[nodiscard]] static int clampChunkDurationMs(int ms);
    [[nodiscard]] int effectiveChunkDurationMs() const {
        return clampChunkDurationMs(chunkDurationMs);
    }

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
    // HTTP path of the transcription endpoint, relative to the base URL.
    [[nodiscard]] QString transcriptionPath() const {
        return kind == SttServiceKind::WhisperCpp ? QStringLiteral("/inference")
                                                  : QStringLiteral("/audio/transcriptions");
    }

    [[nodiscard]] QJsonObject toJson() const;
    static SttServiceConfig fromJson(const QJsonObject &object);
};

} // namespace ocapi
