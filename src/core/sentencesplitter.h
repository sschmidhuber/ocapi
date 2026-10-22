#pragma once

#include <QString>
#include <QStringList>

namespace ocapi {

// Splits streamed assistant text into speakable sentences. Markdown and HTML
// markup (code spans/blocks, emphasis, headings, link URLs, tags) is stripped
// incrementally while text is fed, so constructs spanning multiple feed()
// calls are handled; sentence boundaries are then detected on the clean text
// across newlines, terminators and abbreviations. Pure text logic, no Qt
// object — unit-testable in isolation.
class SentenceSplitter {
public:
    // Appends text (e.g. one streamed token) and returns the sentences that
    // are now complete. The trailing partial sentence stays buffered and is
    // returned by flush().
    QStringList feed(const QString &text);
    // Returns the buffered partial sentence (markup stripped, trimmed).
    [[nodiscard]] QString flush();
    void reset();

    // Strips Markdown/HTML markup from a complete text in one pass: code
    // spans/blocks, emphasis markers, heading markers, blockquote markers,
    // link URLs and HTML tags (only the spoken words remain).
    [[nodiscard]] static QString stripMarkup(const QString &text);

private:
    QStringList drainComplete();
    // Strips markup from \p text using the splitter's incremental state and
    // returns the cleaned chunk to append to the buffer.
    QString stripIncremental(const QString &text);
    // Per-character markup stripping, split out of stripIncremental to keep
    // the cognitive complexity of the state machine manageable. \p i is the
    // current index into \p text; helpers may advance it (e.g. to skip a
    // language tag after a fence). \p out receives the cleaned characters.
    void stripNewline(QString &out);
    void stripBacktick(const QString &text, qsizetype &i, QString &out);
    void stripFenceCandidate(QString &out);
    bool stripOrdinaryChar(QChar ch, QString &out);
    // Sub-handlers of stripOrdinaryChar, split out to keep the cognitive
    // complexity of the state machine below the clang-tidy threshold.
    bool stripLinkChar(QChar ch, QString &out);
    bool stripHtmlChar(QChar ch, QString &out);

    QString m_buffer;
    bool m_inCodeBlock = false;
    bool m_inInlineCode = false;
    bool m_inLinkText = false;
    bool m_skippingLinkUrl = false;
    int m_headingLevel = 0;
    bool m_atLineStart = true;
    bool m_flushing = false;
    bool m_awaitingLinkUrl = false; // ']' seen; '(' or '[' may follow
    bool m_skippingLinkRef = false; // inside the [ref] part of [text][ref]
    bool m_inHtmlTag = false;       // inside <...>
    QString m_htmlTag;              // accumulated HTML tag name
    int m_pendingFence = 0;         // backticks seen at line start (fence candidate)
};

} // namespace ocapi
