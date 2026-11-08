#pragma once

#include "core/debuglog.h"

#include <QString>
#include <QtGlobal>

namespace ocapi::sttDebug {

// Env-gated debug logging for speech-to-text and text-to-speech bugs:
// set OCAPI_DEBUG_STT / OCAPI_DEBUG_TTS to see every request, response and
// error of the STT/TTS clients on stderr (like OCAPI_DEBUG_SSE does for LLM
// streams). Credentials are redacted and long payloads truncated by log(),
// but the output still contains user content: only run it on your own
// traffic and do not paste it into public bug reports.

[[nodiscard]] inline bool sttEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OCAPI_DEBUG_STT");
    return enabled;
}

[[nodiscard]] inline bool ttsEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OCAPI_DEBUG_TTS");
    return enabled;
}

inline void log(const QString &line) {
    qWarning().noquote() << debugSanitize::clip(debugSanitize::redact(line), 1000);
}

} // namespace ocapi::sttDebug
