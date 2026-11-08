#pragma once

#include "core/debuglog.h"

#include <QString>
#include <QtGlobal>

namespace okapi::sttDebug {

// Env-gated debug logging for speech-to-text and text-to-speech bugs:
// set OKAPI_DEBUG_STT / OKAPI_DEBUG_TTS to see every request, response and
// error of the STT/TTS clients on stderr (like OKAPI_DEBUG_SSE does for LLM
// streams). Credentials are redacted and long payloads truncated by log(),
// but the output still contains user content: only run it on your own
// traffic and do not paste it into public bug reports.

[[nodiscard]] inline bool sttEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OKAPI_DEBUG_STT");
    return enabled;
}

[[nodiscard]] inline bool ttsEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OKAPI_DEBUG_TTS");
    return enabled;
}

inline void log(const QString &line) {
    qWarning().noquote() << debugSanitize::clip(debugSanitize::redact(line), 1000);
}

} // namespace okapi::sttDebug
