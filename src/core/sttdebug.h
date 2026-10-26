#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace ocapi {

// Env-gated debug logging for speech-to-text and text-to-speech bugs:
// set OCAPI_DEBUG_STT / OCAPI_DEBUG_TTS to see every request, response and
// error of the STT/TTS clients on stderr (like OCAPI_DEBUG_SSE does for LLM
// streams). Redacts nothing: only run it on your own keys and traffic.
namespace sttDebug {

[[nodiscard]] inline bool sttEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OCAPI_DEBUG_STT");
    return enabled;
}

[[nodiscard]] inline bool ttsEnabled() {
    static const bool enabled = qEnvironmentVariableIsSet("OCAPI_DEBUG_TTS");
    return enabled;
}

inline void log(const QString &line) {
    qWarning().noquote() << line;
}

} // namespace sttDebug

} // namespace ocapi
