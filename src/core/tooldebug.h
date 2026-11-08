#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QtGlobal>

namespace okapi {

class ChatSession;

namespace toolDebug {

// Env-gated debug logging for the MCP tool-call loop: set OKAPI_DEBUG_TOOLS
// to see on stderr which tool calls the model requested, how each call is
// executed (permission decision, server, arguments), which tool-result
// messages are appended to the session, and the exact conversation that is
// sent back to the model after each round. Redacts nothing: only run it on
// your own traffic.

[[nodiscard]] inline bool enabled() {
    static const bool on = qEnvironmentVariableIsSet("OKAPI_DEBUG_TOOLS");
    return on;
}

inline void log(const QString &line) {
    qWarning().noquote() << "[okapi tools]" << line;
}

// Dumps the session's messages (role, content length/snippet, tool calls)
// so it is visible whether tool results reached the conversation.
void logSession(const ChatSession *session, const QString &label);

} // namespace toolDebug
} // namespace okapi
