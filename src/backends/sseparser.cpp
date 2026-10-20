#include "backends/sseparser.h"

#include <QString>

namespace ocapi {

QList<QByteArray> extractCompleteEvents(QByteArray &buffer) {
    QList<QByteArray> events;
    int pos = 0;
    while (true) {
        int idx = buffer.indexOf("\n\n", pos);
        int sepLen = 2;
        if (idx < 0) {
            idx = buffer.indexOf("\r\n\r\n", pos);
            sepLen = 4;
        }
        if (idx < 0)
            break;

        events.append(buffer.mid(pos, idx - pos));
        buffer.remove(0, idx + sepLen - pos);
        pos = 0;
    }
    return events;
}

std::optional<SseEvent> parseSseEventBlock(const QByteArray &rawEvent) {
    if (rawEvent.trimmed().isEmpty())
        return std::nullopt;

    QByteArray event = QStringLiteral("message").toUtf8();
    QByteArray data;
    const auto lines = rawEvent.split('\n');
    for (const auto &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith(':'))
            continue; // comment / heartbeat
        const int colon = trimmed.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray field = trimmed.left(colon);
        QByteArray value = trimmed.mid(colon + 1);
        if (value.startsWith(' '))
            value.remove(0, 1);
        if (field == "event")
            event = value;
        else if (field == "data")
            data.append(value).append('\n');
    }
    if (!data.isEmpty() && data.endsWith('\n'))
        data.chop(1);
    if (data.isEmpty())
        return std::nullopt;

    return SseEvent{event, data};
}

} // namespace ocapi
