#include "backends/sseparser.h"

#include <QString>

namespace ocapi {

namespace {

// Length of the line terminator starting at \p i (CRLF, LF or a lone CR),
// or 0 when a trailing '\r' could still become a CRLF once more bytes arrive.
int terminatorLengthAt(const QByteArray &buffer, int i) {
    const char c = buffer.at(i);
    if (c == '\n')
        return 1;
    if (c != '\r')
        return 0;
    if (i + 1 >= buffer.size())
        return 0;
    return buffer.at(i + 1) == '\n' ? 2 : 1;
}

} // namespace

QList<QByteArray> extractCompleteEvents(QByteArray &buffer) {
    QList<QByteArray> events;
    // An event ends at a blank line: two consecutive line terminators. Scan
    // for the earliest such boundary so that mixed line endings cannot make an
    // early '\r\n\r\n' boundary lose against a later '\n\n' (which merged
    // the two events into one unparsable block and dropped the tokens).
    int start = 0;
    int i = 0;
    while (i < buffer.size()) {
        const int t1 = terminatorLengthAt(buffer, i);
        if (t1 == 0) {
            ++i;
            continue;
        }
        const int j = i + t1;
        const int t2 = j < buffer.size() ? terminatorLengthAt(buffer, j) : 0;
        if (t2 > 0) {
            events.append(buffer.mid(start, i - start));
            start = j + t2;
            i = start;
            continue;
        }
        i = j;
    }
    if (start > 0)
        buffer.remove(0, start);
    return events;
}

std::optional<SseEvent> parseSseEventBlock(const QByteArray &rawEvent) {
    if (rawEvent.trimmed().isEmpty())
        return std::nullopt;

    QByteArray event = QStringLiteral("message").toUtf8();
    QByteArray data;
    // A UTF-8 byte-order mark may precede the first field of the stream; the
    // SSE spec requires it to be ignored, and some gateways/servers prepend
    // it. Left in place it corrupts the first field name ("data" becomes
    // "\xEF\xBB\xBFdata") and the whole first event is dropped.
    const auto lines = rawEvent.split('\n');
    bool firstLine = true;
    for (const auto &line : lines) {
        QByteArray trimmed = line.trimmed();
        if (firstLine) {
            firstLine = false;
            if (trimmed.startsWith("\xEF\xBB\xBF"))
                trimmed.remove(0, 3);
        }
        if (trimmed.startsWith(':'))
            continue; // comment / heartbeat
        const qsizetype colon = trimmed.indexOf(':');
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
