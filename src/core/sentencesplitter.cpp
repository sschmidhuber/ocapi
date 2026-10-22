#include "core/sentencesplitter.h"

namespace ocapi {

namespace {

bool isSentenceTerminator(QChar ch) {
    return ch == QLatin1Char('.') || ch == QLatin1Char('!') || ch == QLatin1Char('?') ||
           ch == QChar(0x2026) || ch == QChar(0xFF01) || ch == QChar(0xFF1F) || ch == QChar(0x3002);
}

bool isWhitespace(QChar ch) {
    return ch.isSpace();
}

// Common abbreviations that end with a dot but do not end a sentence.
bool isAbbreviation(QString word) {
    static const QStringList abbreviations{
        QStringLiteral("mr"),  QStringLiteral("mrs"),  QStringLiteral("ms"),
        QStringLiteral("dr"),  QStringLiteral("st"),   QStringLiteral("jr"),
        QStringLiteral("sr"),  QStringLiteral("vs"),   QStringLiteral("etc"),
        QStringLiteral("e.g"), QStringLiteral("i.e"),  QStringLiteral("cf"),
        QStringLiteral("al"),  QStringLiteral("ca"),   QStringLiteral("approx"),
        QStringLiteral("no"),  QStringLiteral("vol"),  QStringLiteral("fig"),
        QStringLiteral("pp"),  QStringLiteral("p"),    QStringLiteral("sec"),
        QStringLiteral("min"), QStringLiteral("max"),  QStringLiteral("dept"),
        QStringLiteral("est"), QStringLiteral("inc"),  QStringLiteral("ltd"),
        QStringLiteral("co"),  QStringLiteral("corp"), QStringLiteral("jan"),
        QStringLiteral("feb"), QStringLiteral("mar"),  QStringLiteral("apr"),
        QStringLiteral("jun"), QStringLiteral("jul"),  QStringLiteral("aug"),
        QStringLiteral("sep"), QStringLiteral("sept"), QStringLiteral("oct"),
        QStringLiteral("nov"), QStringLiteral("dec"),  QStringLiteral("ph.d"),
        QStringLiteral("u.s"), QStringLiteral("u.k"),  QStringLiteral("a.m"),
        QStringLiteral("p.m"),
    };
    word = word.toLower();
    while (word.endsWith(QLatin1Char('.')))
        word.chop(1);
    return abbreviations.contains(word);
}

// Returns the word (letters, digits, dots, hyphens) that ends at \p dotIndex.
QString wordEndingAt(const QString &text, qsizetype dotIndex) {
    qsizetype start = dotIndex;
    while (start > 0) {
        const QChar prev = text.at(start - 1);
        if (prev.isLetterOrNumber() || prev == QLatin1Char('.') || prev == QLatin1Char('-'))
            --start;
        else
            break;
    }
    return text.mid(start, dotIndex - start + 1);
}

// Finds the index just past the end of the sentence that starts at \p start,
// or -1 when the buffer does not yet contain a complete sentence.
qsizetype sentenceEnd(const QString &text, qsizetype start, bool flush) {
    for (qsizetype i = start; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\n'))
            return i + 1; // a newline ends the sentence even without punctuation
        if (!isSentenceTerminator(ch))
            continue;
        const qsizetype next = i + 1;
        if (next >= text.size()) {
            // Terminator at the very end of the buffer: while flushing the
            // sentence is complete. During streaming, wait for a following
            // character — a trailing "." may still grow into an ellipsis
            // ("...") or turn out to be an abbreviation ("Mr.").
            return flush ? next : -1;
        }
        const QChar after = text.at(next);
        if (!isWhitespace(after) && after != QLatin1Char('"') && after != QChar(0x201D) &&
            after != QLatin1Char(')') && after != QLatin1Char(']'))
            continue; // "file.txt", "example.com", "3.5", "U.S.A"
        if (ch == QLatin1Char('.')) {
            // A dot followed by another dot is part of an ellipsis ("...").
            if (next + 1 < text.size() && text.at(next + 1) == QLatin1Char('.'))
                continue;
            // Abbreviations ("Mr.", "etc.", "e.g.") do not end a sentence.
            if (isAbbreviation(wordEndingAt(text, i)))
                continue;
        }
        return next;
    }
    return -1;
}

} // namespace

QString SentenceSplitter::stripMarkup(const QString &text) {
    SentenceSplitter splitter;
    QString out = splitter.stripIncremental(text);
    if (splitter.m_pendingFence > 0)
        out += QString(splitter.m_pendingFence, QLatin1Char('`'));
    return out;
}

QStringList SentenceSplitter::feed(const QString &text) {
    if (!text.isEmpty())
        m_buffer += stripIncremental(text);
    return drainComplete();
}

QString SentenceSplitter::flush() {
    // Flush mode: a terminator at the very end of the buffer also ends the
    // sentence, so "Hello world." is emitted even without trailing space.
    m_flushing = true;
    const QStringList complete = drainComplete();
    m_flushing = false;
    QString result;
    if (!complete.isEmpty())
        result = complete.join(QLatin1Char(' '));
    const QString rest = m_buffer.trimmed();
    m_buffer.clear();
    if (!result.isEmpty() && !rest.isEmpty())
        result += QLatin1Char(' ');
    result += rest;
    return result;
}

void SentenceSplitter::reset() {
    m_buffer.clear();
    m_inCodeBlock = false;
    m_inInlineCode = false;
    m_inLinkText = false;
    m_skippingLinkUrl = false;
    m_headingLevel = 0;
    m_atLineStart = true;
    m_flushing = false;
    m_awaitingLinkUrl = false;
    m_skippingLinkRef = false;
    m_inHtmlTag = false;
    m_htmlTag.clear();
    m_pendingFence = 0;
}

QStringList SentenceSplitter::drainComplete() {
    QStringList sentences;
    qsizetype start = 0;
    while (start < m_buffer.size()) {
        while (start < m_buffer.size() && isWhitespace(m_buffer.at(start)))
            ++start;
        if (start >= m_buffer.size())
            break;
        const qsizetype end = sentenceEnd(m_buffer, start, m_flushing);
        if (end < 0)
            break;
        const QString sentence = m_buffer.mid(start, end - start).trimmed();
        if (!sentence.isEmpty())
            sentences.push_back(sentence);
        start = end;
    }
    m_buffer.remove(0, start);
    return sentences;
}

QString SentenceSplitter::stripIncremental(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\n')) {
            stripNewline(out);
            continue;
        }
        if (ch == QLatin1Char('`')) {
            stripBacktick(text, i, out);
            continue;
        }
        stripFenceCandidate(out);
        if (stripOrdinaryChar(ch, out))
            continue;
        out += ch;
        m_atLineStart = false;
    }
    return out;
}

// Handles a newline: resets per-line state, flushes a pending fence
// candidate as literal backticks and emits the newline (unless inside a
// code block).
void SentenceSplitter::stripNewline(QString &out) {
    m_inInlineCode = false;
    m_headingLevel = 0;
    if (m_pendingFence > 0) {
        // A fence candidate ended at the newline: literal backticks.
        if (!m_inCodeBlock)
            out += QString(m_pendingFence, QLatin1Char('`'));
        m_pendingFence = 0;
    }
    m_atLineStart = true;
    if (!m_inCodeBlock)
        out += QLatin1Char('\n');
}

// Handles a backtick: backticks at the start of a line form a code fence
// when there are three or more (a fence also closes a code block); fewer
// backticks are literal text or an inline code span. The decision is
// deferred to the following character, so fences and code spans split
// across feed() calls are handled.
void SentenceSplitter::stripBacktick(const QString &text, qsizetype &i, QString &out) {
    Q_UNUSED(out);
    if (m_pendingFence == 1) {
        // Second backtick of a fence candidate.
        ++m_pendingFence;
        return;
    }
    if (m_pendingFence == 2) {
        // Third backtick at line start: a code fence.
        m_pendingFence = 0;
        m_inCodeBlock = !m_inCodeBlock;
        while (i + 1 < text.size() && text.at(i + 1) != QLatin1Char('\n'))
            ++i; // skip the language tag
        m_atLineStart = false;
        return;
    }
    if (m_atLineStart && !m_inInlineCode) {
        // First backtick at line start: start a fence candidate.
        m_pendingFence = 1;
        m_atLineStart = false;
        return;
    }
    if (!m_inCodeBlock)
        m_inInlineCode = !m_inInlineCode;
    m_atLineStart = false;
}

// Resolves a pending fence candidate when a non-backtick, non-newline
// character arrives: the buffered backticks were not a fence and become
// an inline code span or literal text.
void SentenceSplitter::stripFenceCandidate(QString &out) {
    if (m_pendingFence == 1) {
        // A single backtick at line start was not a fence: it opens an
        // inline code span (or is content inside a code block).
        m_pendingFence = 0;
        if (!m_inCodeBlock)
            m_inInlineCode = true;
    } else if (m_pendingFence == 2) {
        // A fence candidate interrupted by ordinary text: the buffered
        // backticks were literal text.
        if (!m_inCodeBlock)
            out += QStringLiteral("``");
        m_pendingFence = 0;
    }
}

// Handles one ordinary (non-newline, non-backtick) character: code blocks
// and spans are skipped, headings/emphasis/blockquote markers are dropped,
// link URLs and HTML tags are stripped. Returns true when the character
// was fully handled (not emitted to \p out).
bool SentenceSplitter::stripOrdinaryChar(QChar ch, QString &out) {
    if (m_inCodeBlock || m_inInlineCode) {
        m_atLineStart = false;
        return true;
    }
    if (ch == QLatin1Char('#') && m_atLineStart) {
        m_headingLevel = 1;
        m_atLineStart = false;
        return true;
    }
    if (m_headingLevel > 0) {
        if (ch == QLatin1Char('#'))
            return true;    // additional '#' of the heading marker
        m_headingLevel = 0; // first non-marker character starts the text
    }
    if (ch == QLatin1Char('*') || ch == QLatin1Char('_') || ch == QLatin1Char('~'))
        return true; // emphasis/strikethrough markers
    if (m_skippingLinkUrl) {
        if (ch == QLatin1Char(')'))
            m_skippingLinkUrl = false;
        m_atLineStart = false;
        return true;
    }
    if (m_inLinkText) {
        if (ch == QLatin1Char(']')) {
            // End of [text]; a '(' right after starts the URL part.
            m_inLinkText = false;
            m_awaitingLinkUrl = true;
        } else {
            out += ch;
        }
        m_atLineStart = false;
        return true;
    }
    if (m_awaitingLinkUrl) {
        if (ch == QLatin1Char('(')) {
            // Inline link [text](url): drop the URL, keep the text.
            m_awaitingLinkUrl = false;
            m_skippingLinkUrl = true;
        } else if (ch == QLatin1Char('[')) {
            // Reference-style link [text][ref]: drop the reference part.
            m_awaitingLinkUrl = false;
            m_skippingLinkRef = true;
        } else if (!ch.isSpace()) {
            // Not a link after all: the ']' was literal text.
            m_awaitingLinkUrl = false;
            out += ch;
        }
        m_atLineStart = false;
        return true;
    }
    if (m_skippingLinkRef) {
        if (ch == QLatin1Char(']'))
            m_skippingLinkRef = false;
        m_atLineStart = false;
        return true;
    }
    if (ch == QLatin1Char('[')) {
        m_inLinkText = true;
        m_atLineStart = false;
        return true;
    }
    if (m_inHtmlTag) {
        if (ch == QLatin1Char('>')) {
            m_inHtmlTag = false;
            if (m_htmlTag.toLower().startsWith(QStringLiteral("br")))
                out += QLatin1Char('\n');
            m_htmlTag.clear();
        } else {
            m_htmlTag += ch;
        }
        m_atLineStart = false;
        return true;
    }
    if (ch == QLatin1Char('<')) {
        m_inHtmlTag = true;
        m_htmlTag.clear();
        m_atLineStart = false;
        return true;
    }
    if (ch == QLatin1Char('>') && m_atLineStart)
        return true; // blockquote marker
    return false;
}

} // namespace ocapi
