#pragma once

#include <QByteArray>
#include <QList>
#include <optional>

namespace ocapi {

struct SseEvent {
    QByteArray event;
    QByteArray data;
};

// Extracts complete SSE event blocks from \a buffer. A block is terminated
// by a blank line ("\n\n" or "\r\n\r\n"). The buffer is modified in place to
// contain only the remaining incomplete trailing data.
[[nodiscard]] QList<QByteArray> extractCompleteEvents(QByteArray& buffer);

// Parses a single raw SSE event block (without the terminating blank line)
// into an event name and data payload. Returns std::nullopt when the block
// carries no data (e.g. comment-only / heartbeat blocks).
[[nodiscard]] std::optional<SseEvent> parseSseEventBlock(const QByteArray& rawEvent);

} // namespace ocapi
