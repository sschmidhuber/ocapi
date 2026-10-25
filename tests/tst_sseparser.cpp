#include "backends/sseparser.h"

#include <QtTest>

using namespace ocapi;

class TestSseParser : public QObject {
    Q_OBJECT

private slots:
    void extractSingleCompleteEvent();
    void extractMultipleEvents();
    void extractLeavesIncompleteDataInBuffer();
    void extractHandlesCrLfLineEndings();
    void extractHandlesMixedLineEndings();
    void extractHandlesCrLfFollowedByLfEvents();
    void extractHandlesLfFollowedByCrLfEvents();
    void extractHandlesCrOnlyTerminators();
    void extractKeepsTrailingCrAwaitingCrlf();
    void extractEmptyBufferYieldsNothing();
    void parseSimpleDataLine();
    void parseEventAndDataFields();
    void parseMultiLineDataIsJoinedWithNewline();
    void parseStripsSingleLeadingSpaceAfterColon();
    void parseKeepsExtraSpacesAfterColon();
    void parseIgnoresCommentLines();
    void parseIgnoresLinesWithoutColon();
    void parseDefaultsEventNameToMessage();
    void parseReturnsNulloptForEmptyBlock();
    void parseReturnsNulloptForCommentOnlyBlock();
    void parseReturnsNulloptWhenOnlyEventNoData();
    void endToEndTypicalOpenAiStream();
};

void TestSseParser::extractSingleCompleteEvent() {
    QByteArray buffer = "data: hello\n\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first(), QByteArray("data: hello"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractMultipleEvents() {
    QByteArray buffer = "data: one\n\ndata: two\n\ndata: three\n\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(0), QByteArray("data: one"));
    QCOMPARE(events.at(1), QByteArray("data: two"));
    QCOMPARE(events.at(2), QByteArray("data: three"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractLeavesIncompleteDataInBuffer() {
    QByteArray buffer = "data: complete\n\ndata: partial";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first(), QByteArray("data: complete"));
    QCOMPARE(buffer, QByteArray("data: partial"));

    // Feeding more data and re-extracting must yield the now-complete event.
    buffer.append(" data\n\n");
    const auto more = extractCompleteEvents(buffer);
    QCOMPARE(more.size(), 1);
    QCOMPARE(more.first(), QByteArray("data: partial data"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractHandlesCrLfLineEndings() {
    QByteArray buffer = "data: crlf\r\n\r\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first(), QByteArray("data: crlf"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractHandlesMixedLineEndings() {
    QByteArray buffer = "data: a\n\ndata: b\r\n\r\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0), QByteArray("data: a"));
    QCOMPARE(events.at(1), QByteArray("data: b"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractHandlesCrLfFollowedByLfEvents() {
    // Regression: an early CRLF blank line must win over a later LF blank
    // line. The old indexOf-based scan found "\n\n" inside the second
    // event's "\r\n\r\n" first context and merged both events into one
    // block, dropping the stream's leading tokens.
    QByteArray buffer = "data: one\r\n\r\ndata: two\n\ndata: three\n\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(0), QByteArray("data: one"));
    QCOMPARE(events.at(1), QByteArray("data: two"));
    QCOMPARE(events.at(2), QByteArray("data: three"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractHandlesLfFollowedByCrLfEvents() {
    QByteArray buffer = "data: one\n\ndata: two\r\n\r\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0), QByteArray("data: one"));
    QCOMPARE(events.at(1), QByteArray("data: two"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractHandlesCrOnlyTerminators() {
    // The SSE spec allows a bare CR as a line terminator.
    QByteArray buffer = "data: one\r\rdata: two\r\r\n";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0), QByteArray("data: one"));
    QCOMPARE(events.at(1), QByteArray("data: two"));
    QVERIFY(buffer.isEmpty());

    // A trailing lone '\r' may still become a CRLF; it stays buffered like an
    // unterminated LF event.
    QByteArray partial = "data: one\r\rdata: two\r";
    const auto first = extractCompleteEvents(partial);
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.at(0), QByteArray("data: one"));
    QCOMPARE(partial, QByteArray("data: two\r"));
}

void TestSseParser::extractKeepsTrailingCrAwaitingCrlf() {
    // A trailing '\r' may still become a CRLF terminator; it must stay buffered.
    QByteArray buffer = "data: one\n\ndata: two\r";
    const auto events = extractCompleteEvents(buffer);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0), QByteArray("data: one"));
    QCOMPARE(buffer, QByteArray("data: two\r"));

    buffer.append("\n\r\n");
    const auto more = extractCompleteEvents(buffer);
    QCOMPARE(more.size(), 1);
    QCOMPARE(more.first(), QByteArray("data: two"));
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::extractEmptyBufferYieldsNothing() {
    QByteArray buffer;
    QVERIFY(extractCompleteEvents(buffer).isEmpty());
    QVERIFY(buffer.isEmpty());
}

void TestSseParser::parseSimpleDataLine() {
    const auto result = parseSseEventBlock("data: hello world");
    QVERIFY(result.has_value());
    QCOMPARE(result->event, QByteArray("message"));
    QCOMPARE(result->data, QByteArray("hello world"));
}

void TestSseParser::parseEventAndDataFields() {
    const auto result = parseSseEventBlock("event: content_block_delta\ndata: {\"text\":\"hi\"}");
    QVERIFY(result.has_value());
    QCOMPARE(result->event, QByteArray("content_block_delta"));
    QCOMPARE(result->data, QByteArray("{\"text\":\"hi\"}"));
}

void TestSseParser::parseMultiLineDataIsJoinedWithNewline() {
    const auto result = parseSseEventBlock("data: line1\ndata: line2\ndata: line3");
    QVERIFY(result.has_value());
    QCOMPARE(result->data, QByteArray("line1\nline2\nline3"));
}

void TestSseParser::parseStripsSingleLeadingSpaceAfterColon() {
    // Per the SSE spec, a single leading space after the colon is removed.
    const auto result = parseSseEventBlock("data: value");
    QVERIFY(result.has_value());
    QCOMPARE(result->data, QByteArray("value"));
}

void TestSseParser::parseKeepsExtraSpacesAfterColon() {
    const auto result = parseSseEventBlock("data:  two spaces");
    QVERIFY(result.has_value());
    QCOMPARE(result->data, QByteArray(" two spaces"));
}

void TestSseParser::parseIgnoresCommentLines() {
    const auto result = parseSseEventBlock(": this is a comment\ndata: real");
    QVERIFY(result.has_value());
    QCOMPARE(result->data, QByteArray("real"));
}

void TestSseParser::parseIgnoresLinesWithoutColon() {
    const auto result = parseSseEventBlock("garbage line\ndata: ok");
    QVERIFY(result.has_value());
    QCOMPARE(result->data, QByteArray("ok"));
}

void TestSseParser::parseDefaultsEventNameToMessage() {
    const auto result = parseSseEventBlock("data: x");
    QVERIFY(result.has_value());
    QCOMPARE(result->event, QByteArray("message"));
}

void TestSseParser::parseReturnsNulloptForEmptyBlock() {
    QVERIFY(!parseSseEventBlock("").has_value());
    QVERIFY(!parseSseEventBlock("   ").has_value());
    QVERIFY(!parseSseEventBlock("\n\n").has_value());
}

void TestSseParser::parseReturnsNulloptForCommentOnlyBlock() {
    // Heartbeat / keep-alive comments must not produce events.
    QVERIFY(!parseSseEventBlock(": keep-alive").has_value());
    QVERIFY(!parseSseEventBlock(":\n: another comment").has_value());
}

void TestSseParser::parseReturnsNulloptWhenOnlyEventNoData() {
    QVERIFY(!parseSseEventBlock("event: ping").has_value());
}

void TestSseParser::endToEndTypicalOpenAiStream() {
    // Simulates a realistic chunked OpenAI-compatible SSE stream arriving
    // in two TCP reads.
    QByteArray buffer;
    QList<SseEvent> received;

    buffer.append("data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}]}\n\n"
                  "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}]}\n\n"
                  "data: {\"choices\":[{\"delta\":{},\"finish_rea");
    for (const auto &raw : extractCompleteEvents(buffer)) {
        const auto parsed = parseSseEventBlock(raw);
        if (parsed)
            received.append(*parsed);
    }
    QCOMPARE(received.size(), 2);
    QCOMPARE(received.at(0).data, QByteArray("{\"choices\":[{\"delta\":{\"content\":\"Hello\"}]}"));
    QCOMPARE(received.at(1).data,
             QByteArray("{\"choices\":[{\"delta\":{\"content\":\" world\"}]}"));

    // Second read completes the final event plus the [DONE] sentinel.
    buffer.append("son\":\"stop\"}]}\n\n"
                  "data: [DONE]\n\n");
    for (const auto &raw : extractCompleteEvents(buffer)) {
        const auto parsed = parseSseEventBlock(raw);
        if (parsed)
            received.append(*parsed);
    }
    QCOMPARE(received.size(), 4);
    QCOMPARE(received.at(2).data,
             QByteArray("{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}"));
    QCOMPARE(received.at(3).data, QByteArray("[DONE]"));
    QVERIFY(buffer.isEmpty());
}

QTEST_GUILESS_MAIN(TestSseParser)
#include "tst_sseparser.moc"
