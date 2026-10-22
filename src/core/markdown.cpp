#include "core/markdown.h"

#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>

#include <memory>

namespace ocapi {

namespace {

// Maximum number of cached documents. The chat view re-renders a message on
// every streamed token; the cache avoids re-parsing markdown that has not
// changed, and the cap keeps memory bounded for long sessions.
constexpr int kMaxCacheEntries = 256;

// QTextDocument is not copyable, so the cache stores an HTML snapshot and
// rebuilds a fresh document per call. HTML round-trips preserve char formats
// (bold, italic, monospace, links, backgrounds) and block layout.
struct CachedDocument {
    QFont font;
    qreal textWidth = 0;
    QString html;
};

QHash<QString, CachedDocument> &cache() {
    static QHash<QString, CachedDocument> documents;
    return documents;
}

QString cacheKey(const QString &markdown, int maxTextWidth, const QFont &font) {
    return QStringLiteral("%1\x1F%2\x1F%3")
        .arg(markdown, QString::number(maxTextWidth), font.key());
}

bool isFenceLine(const QString &line) {
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"));
}

bool isHeadingLine(const QString &line, int *level) {
    int hashes = 0;
    while (hashes < line.size() && line.at(hashes) == QLatin1Char('#'))
        ++hashes;
    if (hashes == 0 || hashes > 6)
        return false;
    if (hashes < line.size() && line.at(hashes) != QLatin1Char(' '))
        return false;
    *level = hashes;
    return true;
}

bool isUnorderedListItem(const QString &line) {
    const QString trimmed = line.trimmed();
    return trimmed.startsWith(QStringLiteral("- ")) || trimmed.startsWith(QStringLiteral("* ")) ||
           trimmed.startsWith(QStringLiteral("+ "));
}

bool isOrderedListItem(const QString &line, int *number) {
    const QString trimmed = line.trimmed();
    int i = 0;
    while (i < trimmed.size() && trimmed.at(i).isDigit())
        ++i;
    if (i == 0 || i >= trimmed.size() || trimmed.at(i) != QLatin1Char('.'))
        return false;
    if (i + 1 >= trimmed.size() || trimmed.at(i + 1) != QLatin1Char(' '))
        return false;
    *number = trimmed.left(i).toInt();
    return true;
}

bool isBlockquoteLine(const QString &line) {
    return line.trimmed().startsWith(QLatin1Char('>'));
}

bool isHorizontalRule(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.size() < 3)
        return false;
    const QChar first = trimmed.at(0);
    if (first != QLatin1Char('-') && first != QLatin1Char('*') && first != QLatin1Char('_'))
        return false;
    for (const QChar &ch : trimmed)
        if (ch != first && ch != QLatin1Char(' '))
            return false;
    return true;
}

bool isMonospace(const QTextCharFormat &format) {
    if (!format.hasProperty(QTextFormat::FontFamilies))
        return false;
    const QStringList families = format.property(QTextFormat::FontFamilies).toStringList();
    return !families.isEmpty() && families.first() == QLatin1String("monospace");
}

// A single styled run of inline text: either literal (code span) or markdown
// source to be interpreted.
struct InlineRun {
    QString text;
    bool literal = false;
};

// Splits inline markdown into literal code spans and interpretable text.
QVector<InlineRun> splitInlineRuns(const QString &text) {
    QVector<InlineRun> runs;
    if (text.isEmpty())
        return runs;
    QString remaining = text;
    while (!remaining.isEmpty()) {
        const int codeStart = remaining.indexOf(QLatin1Char('`'));
        if (codeStart < 0) {
            runs.push_back({remaining, false});
            break;
        }
        if (codeStart > 0)
            runs.push_back({remaining.left(codeStart), false});
        const int codeEnd = remaining.indexOf(QLatin1Char('`'), codeStart + 1);
        if (codeEnd < 0) {
            runs.push_back({remaining.mid(codeStart), false});
            break;
        }
        runs.push_back({remaining.mid(codeStart + 1, codeEnd - codeStart - 1), true});
        remaining = remaining.mid(codeEnd + 1);
    }
    return runs;
}

// Interprets one markdown formatting construct starting at position i in text.
// On success, fills outFormat with the emphasis to apply to [start, end),
// markerLength with the length of each marker to strip, and returns the
// position just past the construct.
struct InlineMatch {
    int start = -1;
    int end = -1;
    int markerLength = 0;
    QTextCharFormat format;
};

InlineMatch matchInlineAt(const QString &text, int i) {
    InlineMatch match;
    const QChar ch = text.at(i);
    // Link: [text](url)
    if (ch == QLatin1Char('[')) {
        const int close = text.indexOf(QLatin1Char(']'), i + 1);
        if (close > i + 1 && close + 1 < text.size() && text.at(close + 1) == QLatin1Char('(')) {
            const int urlEnd = text.indexOf(QLatin1Char(')'), close + 2);
            if (urlEnd > close + 2) {
                match.start = i;
                match.end = urlEnd + 1;
                match.format.setForeground(QColor(0x2a, 0x82, 0xda));
                match.format.setFontUnderline(true);
                match.format.setAnchor(true);
                match.format.setAnchorHref(text.mid(close + 2, urlEnd - close - 2));
                return match;
            }
        }
        return match;
    }
    const bool doubleMarker =
        i + 1 < text.size() && text.at(i + 1) == ch &&
        (ch == QLatin1Char('*') || ch == QLatin1Char('_') || ch == QLatin1Char('~'));
    if (doubleMarker) {
        const QString needle = QString(ch) + ch;
        const int close = text.indexOf(needle, i + 2);
        if (close > i + 2) {
            match.start = i;
            match.end = close + 2;
            match.markerLength = 2;
            if (ch == QLatin1Char('~'))
                match.format.setFontStrikeOut(true);
            else
                match.format.setFontWeight(QFont::Bold);
            return match;
        }
        return match;
    }
    if (ch == QLatin1Char('*') || ch == QLatin1Char('_')) {
        int close = -1;
        for (int j = i + 1; j < text.size(); ++j) {
            if (text.at(j) == ch && (j + 1 >= text.size() || text.at(j + 1) != ch)) {
                close = j;
                break;
            }
        }
        if (close > i + 1) {
            match.start = i;
            match.end = close + 1;
            match.markerLength = 1;
            match.format.setFontItalic(true);
            return match;
        }
    }
    return match;
}

// Inserts inline markdown into the current block: code spans are inserted
// literally with a monospace format, and emphasis/links are applied to the
// remaining text. The whole run is processed in one pass, so marker removal
// never shifts the positions of text that is yet to be processed.
void insertInline(QTextCursor &cursor, const QString &text) {
    for (const InlineRun &run : splitInlineRuns(text)) {
        if (run.literal) {
            QTextCharFormat codeFormat;
            codeFormat.setFontFamilies({QStringLiteral("monospace")});
            codeFormat.setBackground(QColor(0x40, 0x40, 0x40, 0x60));
            cursor.insertText(run.text, codeFormat);
            continue;
        }
        QString remaining = run.text;
        while (!remaining.isEmpty()) {
            // Find the next formatting construct.
            int matchPos = -1;
            InlineMatch best;
            for (int i = 0; i < remaining.size(); ++i) {
                const InlineMatch m = matchInlineAt(remaining, i);
                if (m.start >= 0) {
                    matchPos = i;
                    best = m;
                    break;
                }
            }
            if (matchPos < 0) {
                cursor.insertText(remaining);
                break;
            }
            if (matchPos > 0)
                cursor.insertText(remaining.left(matchPos));
            // Insert the inner text of the construct without its markers.
            QString inner;
            if (best.markerLength > 0) {
                inner = remaining.mid(best.start + best.markerLength,
                                      best.end - best.start - 2 * best.markerLength);
            } else {
                // Link: [text](url) -> insert only the link text.
                const int close = remaining.indexOf(QLatin1Char(']'), best.start + 1);
                inner = remaining.mid(best.start + 1, close - best.start - 1);
            }
            const int innerStart = cursor.position();
            cursor.insertText(inner);
            QTextCursor formatCursor(cursor.document());
            formatCursor.setPosition(innerStart);
            formatCursor.setPosition(innerStart + inner.size(), QTextCursor::KeepAnchor);
            formatCursor.mergeCharFormat(best.format);
            remaining = remaining.mid(best.end);
        }
    }
}

} // namespace

std::unique_ptr<QTextDocument> copyDocument(const CachedDocument &source) {
    auto copy = std::make_unique<QTextDocument>();
    copy->setDefaultFont(source.font);
    copy->setTextWidth(source.textWidth);
    copy->setHtml(source.html);
    return copy;
}

std::unique_ptr<QTextDocument> MarkdownRenderer::render(const QString &markdown, int maxTextWidth,
                                                        const QFont &font) {
    const QString key = cacheKey(markdown, maxTextWidth, font);
    auto &documents = cache();
    const auto it = documents.constFind(key);
    if (it != documents.constEnd())
        return copyDocument(it.value());

    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setTextWidth(maxTextWidth);
    renderInto(doc, markdown, font);

    if (documents.size() >= kMaxCacheEntries)
        documents.clear();
    const CachedDocument cached{font, static_cast<qreal>(maxTextWidth), doc.toHtml()};
    documents.insert(key, cached);
    return copyDocument(cached);
}

void MarkdownRenderer::clearCache() {
    cache().clear();
}

void MarkdownRenderer::renderInto(QTextDocument &doc, const QString &markdown, const QFont &font) {
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    int i = 0;
    bool firstBlock = true;

    auto ensureBlock = [&]() {
        if (firstBlock) {
            firstBlock = false;
            return;
        }
        cursor.insertBlock();
    };

    while (i < lines.size()) {
        const QString &line = lines.at(i);

        if (line.trimmed().isEmpty()) {
            ++i;
            continue;
        }

        // Fenced code block.
        if (isFenceLine(line)) {
            QStringList codeLines;
            ++i;
            while (i < lines.size() && !isFenceLine(lines.at(i))) {
                codeLines.push_back(lines.at(i));
                ++i;
            }
            if (i < lines.size())
                ++i; // skip closing fence
            ensureBlock();
            QTextBlockFormat codeBlockFormat;
            codeBlockFormat.setBackground(QColor(0x30, 0x30, 0x30, 0x60));
            codeBlockFormat.setLeftMargin(8);
            codeBlockFormat.setRightMargin(8);
            codeBlockFormat.setTopMargin(4);
            codeBlockFormat.setBottomMargin(4);
            cursor.setBlockFormat(codeBlockFormat);
            QTextCharFormat codeFormat;
            codeFormat.setFontFamilies({QStringLiteral("monospace")});
            cursor.insertText(codeLines.join(QLatin1Char('\n')), codeFormat);
            continue;
        }

        // Heading.
        int level = 0;
        if (isHeadingLine(line, &level)) {
            ensureBlock();
            QTextBlockFormat headingBlockFormat;
            headingBlockFormat.setTopMargin(level <= 2 ? 8 : 4);
            headingBlockFormat.setBottomMargin(2);
            cursor.setBlockFormat(headingBlockFormat);
            const QString headingText = line.trimmed().mid(level).trimmed();
            insertInline(cursor, headingText);
            if (!headingText.isEmpty()) {
                QTextCharFormat headingFormat;
                headingFormat.setFontWeight(QFont::Bold);
                const qreal baseSize = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
                const qreal scale = level == 1 ? 1.5 : (level == 2 ? 1.3 : 1.1);
                headingFormat.setFontPointSize(baseSize * scale);
                QTextCursor blockCursor(cursor.block());
                blockCursor.select(QTextCursor::BlockUnderCursor);
                blockCursor.mergeCharFormat(headingFormat);
            }
            ++i;
            continue;
        }

        // Horizontal rule.
        if (isHorizontalRule(line)) {
            ensureBlock();
            QTextBlockFormat hrFormat;
            hrFormat.setTopMargin(6);
            hrFormat.setBottomMargin(6);
            cursor.setBlockFormat(hrFormat);
            QTextCharFormat hrCharFormat;
            hrCharFormat.setForeground(QColor(0x80, 0x80, 0x80));
            cursor.insertText(QString(40, QChar(0x2500)), hrCharFormat);
            ++i;
            continue;
        }

        // Blockquote (possibly multi-line).
        if (isBlockquoteLine(line)) {
            QStringList quoteLines;
            while (i < lines.size() && isBlockquoteLine(lines.at(i))) {
                QString q = lines.at(i).trimmed();
                q.remove(0, 1);
                if (q.startsWith(QLatin1Char(' ')))
                    q.remove(0, 1);
                quoteLines.push_back(q);
                ++i;
            }
            ensureBlock();
            QTextBlockFormat quoteFormat;
            quoteFormat.setLeftMargin(16);
            quoteFormat.setTopMargin(2);
            quoteFormat.setBottomMargin(2);
            cursor.setBlockFormat(quoteFormat);
            QTextCharFormat quoteCharFormat;
            quoteCharFormat.setFontItalic(true);
            quoteCharFormat.setForeground(QColor(0x70, 0x70, 0x70));
            cursor.insertText(quoteLines.join(QLatin1Char('\n')), quoteCharFormat);
            continue;
        }

        // Unordered list item.
        if (isUnorderedListItem(line)) {
            ensureBlock();
            QTextBlockFormat listFormat;
            listFormat.setLeftMargin(16);
            listFormat.setTopMargin(1);
            listFormat.setBottomMargin(1);
            cursor.setBlockFormat(listFormat);
            cursor.insertText(QStringLiteral("\u2022 "));
            insertInline(cursor, line.trimmed().mid(2));
            ++i;
            continue;
        }

        // Ordered list item.
        int number = 0;
        if (isOrderedListItem(line, &number)) {
            ensureBlock();
            QTextBlockFormat listFormat;
            listFormat.setLeftMargin(16);
            listFormat.setTopMargin(1);
            listFormat.setBottomMargin(1);
            cursor.setBlockFormat(listFormat);
            cursor.insertText(QStringLiteral("%1. ").arg(number));
            const QString trimmed = line.trimmed();
            const int dot = trimmed.indexOf(QLatin1Char('.'));
            insertInline(cursor, trimmed.mid(dot + 1).trimmed());
            ++i;
            continue;
        }

        // Plain paragraph: gather consecutive plain lines and join them.
        QStringList paragraphLines;
        while (i < lines.size() && !lines.at(i).trimmed().isEmpty() && !isFenceLine(lines.at(i))) {
            int headingLevel = 0;
            int listNumber = 0;
            if (isHeadingLine(lines.at(i), &headingLevel) || isHorizontalRule(lines.at(i)) ||
                isBlockquoteLine(lines.at(i)) || isUnorderedListItem(lines.at(i)) ||
                isOrderedListItem(lines.at(i), &listNumber))
                break;
            paragraphLines.push_back(lines.at(i));
            ++i;
        }
        if (paragraphLines.isEmpty()) {
            ++i;
            continue;
        }
        ensureBlock();
        insertInline(cursor, paragraphLines.join(QLatin1Char(' ')));
    }

    // Remove the initial empty block if content was added after it.
    QTextCursor start(&doc);
    start.movePosition(QTextCursor::Start);
    if (start.block().text().isEmpty() && doc.blockCount() > 1) {
        start.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        start.removeSelectedText();
    }
}

} // namespace ocapi
