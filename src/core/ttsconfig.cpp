#include "core/ttsconfig.h"

#include <QHash>

namespace okapi {

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
        return QStringLiteral("Mistral AI speech API (Voxtral TTS 3; voice = voice ID, "
                              "e.g. gb_jane_neutral, voice_478; pace via [pace=...] input tags)");
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
        return QStringLiteral("voxtral-mini-tts-3-0");
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
    case TtsServiceKind::OpenAiTts:
        return QStringLiteral("alloy");
    case TtsServiceKind::Mistral:
        return QStringLiteral("gb_jane_neutral");
    case TtsServiceKind::ElevenLabs:
        return QStringLiteral("21m00Tcm4TlvDq8ikWAM"); // "Rachel"
    case TtsServiceKind::Piper:
        return {};
    }
    return {};
}

QStringList TtsServiceConfig::availableVoices(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
    case TtsServiceKind::Groq:
    case TtsServiceKind::OpenAiTts:
        return {
            QStringLiteral("alloy"), QStringLiteral("echo"), QStringLiteral("fable"),
            QStringLiteral("onyx"),  QStringLiteral("nova"), QStringLiteral("shimmer"),
        };
    case TtsServiceKind::Mistral:
        // Mistral Voxtral preset voices (voice_id slugs and IDs from
        // docs.mistral.ai/studio/audio/text-to-speech/voices). Voxtral 2
        // presets are still accepted by Voxtral 3.
        return {
            // Voxtral 2 presets (slugs)
            QStringLiteral("en_paul_neutral"),
            QStringLiteral("en_paul_happy"),
            QStringLiteral("en_paul_cheerful"),
            QStringLiteral("en_paul_excited"),
            QStringLiteral("en_paul_confident"),
            QStringLiteral("en_paul_sad"),
            QStringLiteral("en_paul_frustrated"),
            QStringLiteral("en_paul_angry"),
            QStringLiteral("gb_jane_neutral"),
            QStringLiteral("gb_jane_confident"),
            QStringLiteral("gb_jane_curious"),
            QStringLiteral("gb_jane_confused"),
            QStringLiteral("gb_jane_sarcasm"),
            QStringLiteral("gb_jane_sad"),
            QStringLiteral("gb_jane_shameful"),
            QStringLiteral("gb_jane_jealousy"),
            QStringLiteral("gb_jane_frustrated"),
            QStringLiteral("gb_oliver_neutral"),
            QStringLiteral("gb_oliver_cheerful"),
            QStringLiteral("gb_oliver_excited"),
            QStringLiteral("gb_oliver_confident"),
            QStringLiteral("gb_oliver_curious"),
            QStringLiteral("gb_oliver_sad"),
            QStringLiteral("gb_oliver_angry"),
            QStringLiteral("fr_marie_neutral"),
            QStringLiteral("fr_marie_happy"),
            QStringLiteral("fr_marie_excited"),
            QStringLiteral("fr_marie_curious"),
            QStringLiteral("fr_marie_sad"),
            QStringLiteral("fr_marie_angry"),
            // Voxtral 3 voices (IDs)
            QStringLiteral("voice_478"),
            QStringLiteral("voice_447"),
            QStringLiteral("voice_485"),
            QStringLiteral("voice_3150"),
            QStringLiteral("voice_1787"),
            QStringLiteral("voice_1786"),
            QStringLiteral("voice_1788"),
            QStringLiteral("voice_590"),
            QStringLiteral("voice_591"),
            QStringLiteral("voice_3149"),
            QStringLiteral("voice_3147"),
            QStringLiteral("voice_592"),
            QStringLiteral("voice_3137"),
            QStringLiteral("voice_589"),
            QStringLiteral("voice_3141"),
            QStringLiteral("voice_483"),
            QStringLiteral("voice_481"),
            QStringLiteral("voice_482"),
            QStringLiteral("voice_480"),
            QStringLiteral("voice_479"),
            QStringLiteral("voice_3143"),
            QStringLiteral("voice_3139"),
            QStringLiteral("voice_3153"),
            QStringLiteral("voice_3146"),
            QStringLiteral("voice_3133"),
            QStringLiteral("voice_3136"),
            QStringLiteral("voice_3135"),
            QStringLiteral("voice_3145"),
            QStringLiteral("voice_3134"),
            QStringLiteral("voice_3154"),
            QStringLiteral("voice_3155"),
            QStringLiteral("voice_3132"),
            QStringLiteral("voice_3138"),
            QStringLiteral("voice_3140"),
            QStringLiteral("voice_3144"),
            QStringLiteral("voice_3142"),
        };
    case TtsServiceKind::ElevenLabs:
    case TtsServiceKind::Piper:
        return {};
    }
    return {};
}

bool TtsServiceConfig::supportsSpeed(TtsServiceKind kind) {
    switch (kind) {
    case TtsServiceKind::OpenAiApi:
    case TtsServiceKind::Groq:
    case TtsServiceKind::Mistral:
    case TtsServiceKind::ElevenLabs:
    case TtsServiceKind::OpenAiTts:
    case TtsServiceKind::Piper:
        return true;
    }
    return false;
}

QVector<TtsPace> TtsServiceConfig::availablePaces() {
    return {TtsPace::VerySlow, TtsPace::Slow, TtsPace::Moderate, TtsPace::Fast, TtsPace::VeryFast};
}

QString TtsServiceConfig::mistralPaceTag(TtsPace pace) {
    switch (pace) {
    case TtsPace::VerySlow:
        return QStringLiteral("[pace=very-slow]");
    case TtsPace::Slow:
        return QStringLiteral("[pace=slow]");
    case TtsPace::Moderate:
        return QStringLiteral("[pace=moderate]");
    case TtsPace::Fast:
        return QStringLiteral("[pace=fast]");
    case TtsPace::VeryFast:
        return QStringLiteral("[pace=very-fast]");
    }
    return {};
}

TtsPace TtsServiceConfig::paceFromSpeed(double speed) {
    // Representative factors: very-slow 0.5, slow 0.75, moderate 1.0,
    // fast 1.5, very-fast 2.0.
    if (speed < 0.625)
        return TtsPace::VerySlow;
    if (speed < 0.875)
        return TtsPace::Slow;
    if (speed < 1.25)
        return TtsPace::Moderate;
    if (speed < 1.75)
        return TtsPace::Fast;
    return TtsPace::VeryFast;
}

double TtsServiceConfig::speedFromPace(TtsPace pace) {
    switch (pace) {
    case TtsPace::VerySlow:
        return 0.5;
    case TtsPace::Slow:
        return 0.75;
    case TtsPace::Moderate:
        return defaultSpeed;
    case TtsPace::Fast:
        return 1.5;
    case TtsPace::VeryFast:
        return 2.0;
    }
    return defaultSpeed;
}

QString TtsServiceConfig::speedLabel(double speed, TtsServiceKind kind) {
    if (kind == TtsServiceKind::Mistral) {
        switch (paceFromSpeed(speed)) {
        case TtsPace::VerySlow:
            return QStringLiteral("very slow");
        case TtsPace::Slow:
            return QStringLiteral("slow");
        case TtsPace::Moderate:
            return QStringLiteral("moderate");
        case TtsPace::Fast:
            return QStringLiteral("fast");
        case TtsPace::VeryFast:
            return QStringLiteral("very fast");
        }
    }
    return QStringLiteral("%1\u00d7").arg(QString::number(speed, 'f', 2));
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
    // Mistral serves raw MP3 only when response_format is set; without it
    // the endpoint returns a JSON {"audio_data": "<base64>"} envelope.
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
        {QStringLiteral("speed"), speed},
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
    const double storedSpeed = object.value(QStringLiteral("speed")).toDouble(defaultSpeed);
    config.speed =
        (storedSpeed >= minSpeed && storedSpeed <= maxSpeed) ? storedSpeed : defaultSpeed;
    config.language = object.value(QStringLiteral("language")).toString();
    config.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    return config;
}

} // namespace okapi
