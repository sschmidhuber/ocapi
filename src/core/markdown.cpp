#include "core/markdown.h"

#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextTable>
#include <QTextTableCellFormat>
#include <QTextTableFormat>

#include <algorithm>
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

// A table row: cells split on unescaped pipes, at least 2 columns.
QStringList tableRowCells(const QString &line) {
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|')))
        trimmed.remove(0, 1);
    if (trimmed.endsWith(QLatin1Char('|')) && !trimmed.endsWith(QStringLiteral("\\|")))
        trimmed.chop(1);
    QStringList cells;
    QString cell;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('\\') && i + 1 < trimmed.size() &&
            trimmed.at(i + 1) == QLatin1Char('|')) {
            cell += QLatin1Char('|');
            ++i;
            continue;
        }
        if (ch == QLatin1Char('|')) {
            cells.push_back(cell.trimmed());
            cell.clear();
            continue;
        }
        cell += ch;
    }
    cells.push_back(cell.trimmed());
    return cells;
}

// A delimiter row such as | --- | :---: | ---: | separates header and body.
bool isTableDelimiterRow(const QString &line) {
    const QStringList cells = tableRowCells(line);
    if (cells.size() < 2)
        return false;
    for (const QString &cell : cells) {
        const QString c = cell.trimmed();
        if (c.isEmpty())
            return false;
        int i = 0;
        if (c.at(i) == QLatin1Char(':'))
            ++i;
        const int dashesStart = i;
        while (i < c.size() && c.at(i) == QLatin1Char('-'))
            ++i;
        if (i == dashesStart || i - dashesStart < 1)
            return false;
        if (i < c.size() && c.at(i) == QLatin1Char(':'))
            ++i;
        if (i != c.size())
            return false;
    }
    return true;
}

// A table starts at a pipe row followed by a delimiter row.
bool isTableStart(const QStringList &lines, int i) {
    return i + 1 < lines.size() && tableRowCells(lines.at(i)).size() >= 2 &&
           isTableDelimiterRow(lines.at(i + 1));
}

Qt::Alignment tableCellAlignment(const QString &delimiterCell) {
    const QString c = delimiterCell.trimmed();
    const bool left = c.startsWith(QLatin1Char(':'));
    const bool right = c.endsWith(QLatin1Char(':'));
    if (left && right)
        return Qt::AlignHCenter;
    if (right)
        return Qt::AlignRight;
    return Qt::AlignLeft;
}

bool isHorizontalRule(const QString &line) {
    const QString trimmed = line.trimmed();
    if (trimmed.size() < 3)
        return false;
    const QChar first = trimmed.at(0);
    if (first != QLatin1Char('-') && first != QLatin1Char('*') && first != QLatin1Char('_'))
        return false;
    return std::ranges::all_of(
        trimmed, [&first](const QChar &ch) { return ch == first || ch == QLatin1Char(' '); });
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
        const int codeStart = static_cast<int>(remaining.indexOf(QLatin1Char('`')));
        if (codeStart < 0) {
            runs.push_back({.text = remaining, .literal = false});
            break;
        }
        if (codeStart > 0)
            runs.push_back({.text = remaining.left(codeStart), .literal = false});
        const int codeEnd = static_cast<int>(remaining.indexOf(QLatin1Char('`'), codeStart + 1));
        if (codeEnd < 0) {
            runs.push_back({.text = remaining.mid(codeStart), .literal = false});
            break;
        }
        runs.push_back(
            {.text = remaining.mid(codeStart + 1, codeEnd - codeStart - 1), .literal = true});
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

InlineMatch matchLinkAt(const QString &text, int i) {
    InlineMatch match;
    const int close = static_cast<int>(text.indexOf(QLatin1Char(']'), i + 1));
    if (close <= i + 1 || close + 1 >= text.size() || text.at(close + 1) != QLatin1Char('('))
        return match;
    const int urlEnd = static_cast<int>(text.indexOf(QLatin1Char(')'), close + 2));
    if (urlEnd <= close + 2)
        return match;
    match.start = i;
    match.end = urlEnd + 1;
    match.format.setForeground(QColor(0x2a, 0x82, 0xda));
    match.format.setFontUnderline(true);
    match.format.setAnchor(true);
    match.format.setAnchorHref(text.mid(close + 2, urlEnd - close - 2));
    return match;
}

InlineMatch matchDoubleMarkerAt(const QString &text, int i, QChar ch) {
    InlineMatch match;
    const QString needle = QString(ch) + ch;
    const int close = static_cast<int>(text.indexOf(needle, i + 2));
    if (close <= i + 2)
        return match;
    match.start = i;
    match.end = close + 2;
    match.markerLength = 2;
    if (ch == QLatin1Char('~'))
        match.format.setFontStrikeOut(true);
    else
        match.format.setFontWeight(QFont::Bold);
    return match;
}

InlineMatch matchSingleMarkerAt(const QString &text, int i, QChar ch) {
    InlineMatch match;
    int close = -1;
    for (int j = i + 1; j < text.size(); ++j) {
        if (text.at(j) == ch && (j + 1 >= text.size() || text.at(j + 1) != ch)) {
            close = j;
            break;
        }
    }
    if (close <= i + 1)
        return match;
    match.start = i;
    match.end = close + 1;
    match.markerLength = 1;
    match.format.setFontItalic(true);
    return match;
}

InlineMatch matchInlineAt(const QString &text, int i) {
    const QChar ch = text.at(i);
    if (ch == QLatin1Char('['))
        return matchLinkAt(text, i);
    const bool doubleMarker =
        i + 1 < text.size() && text.at(i + 1) == ch &&
        (ch == QLatin1Char('*') || ch == QLatin1Char('_') || ch == QLatin1Char('~'));
    if (doubleMarker)
        return matchDoubleMarkerAt(text, i, ch);
    if (ch == QLatin1Char('*') || ch == QLatin1Char('_'))
        return matchSingleMarkerAt(text, i, ch);
    return InlineMatch{};
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
                                      best.end - best.start - (2 * best.markerLength));
            } else {
                // Link: [text](url) -> insert only the link text.
                const int close =
                    static_cast<int>(remaining.indexOf(QLatin1Char(']'), best.start + 1));
                inner = remaining.mid(best.start + 1, close - best.start - 1);
            }
            const int innerStart = cursor.position();
            cursor.insertText(inner);
            QTextCursor formatCursor(cursor.document());
            formatCursor.setPosition(innerStart);
            formatCursor.setPosition(innerStart + static_cast<int>(inner.size()),
                                     QTextCursor::KeepAnchor);
            formatCursor.mergeCharFormat(best.format);
            remaining = remaining.mid(best.end);
        }
    }
}

void insertFencedCodeBlock(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i);

void insertHeading(QTextCursor &cursor, bool &firstBlock, const QString &line, int level,
                   const QFont &font);

void insertHorizontalRule(QTextCursor &cursor, bool &firstBlock);

void insertBlockquote(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i);

void insertUnorderedListItem(QTextCursor &cursor, bool &firstBlock, const QString &line);

void insertOrderedListItem(QTextCursor &cursor, bool &firstBlock, const QString &line, int number);

void insertParagraph(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i);

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
    const CachedDocument cached{
        .font = font,
        .textWidth = static_cast<qreal>(maxTextWidth),
        .html = doc.toHtml(),
    };
    documents.insert(key, cached);
    return copyDocument(cached);
}

void MarkdownRenderer::clearCache() {
    cache().clear();
}

namespace {

void insertFencedCodeBlock(QTextCursor &cursor, bool &firstBlock, const QStringList &lines,
                           int &i) {
    QStringList codeLines;
    ++i;
    while (i < lines.size() && !isFenceLine(lines.at(i))) {
        codeLines.push_back(lines.at(i));
        ++i;
    }
    if (i < lines.size())
        ++i; // skip closing fence
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
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
}

void insertHeading(QTextCursor &cursor, bool &firstBlock, const QString &line, int level,
                   const QFont &font) {
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    QTextBlockFormat headingBlockFormat;
    headingBlockFormat.setTopMargin(level <= 2 ? 8 : 4);
    headingBlockFormat.setBottomMargin(2);
    cursor.setBlockFormat(headingBlockFormat);
    const QString headingText = line.trimmed().mid(level).trimmed();
    insertInline(cursor, headingText);
    if (headingText.isEmpty())
        return;
    QTextCharFormat headingFormat;
    headingFormat.setFontWeight(QFont::Bold);
    const qreal baseSize = font.pointSizeF() > 0 ? font.pointSizeF() : 10.0;
    qreal scale = 1.1;
    if (level == 1)
        scale = 1.5;
    else if (level == 2)
        scale = 1.3;
    headingFormat.setFontPointSize(baseSize * scale);
    QTextCursor blockCursor(cursor.block());
    blockCursor.select(QTextCursor::BlockUnderCursor);
    blockCursor.mergeCharFormat(headingFormat);
}

void insertHorizontalRule(QTextCursor &cursor, bool &firstBlock) {
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    QTextBlockFormat hrFormat;
    hrFormat.setTopMargin(6);
    hrFormat.setBottomMargin(6);
    cursor.setBlockFormat(hrFormat);
    QTextCharFormat hrCharFormat;
    hrCharFormat.setForeground(QColor(0x80, 0x80, 0x80));
    cursor.insertText(QString(40, QChar(0x2500)), hrCharFormat);
}

void insertBlockquote(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i) {
    QStringList quoteLines;
    while (i < lines.size() && isBlockquoteLine(lines.at(i))) {
        QString q = lines.at(i).trimmed();
        q.remove(0, 1);
        if (q.startsWith(QLatin1Char(' ')))
            q.remove(0, 1);
        quoteLines.push_back(q);
        ++i;
    }
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    QTextBlockFormat quoteFormat;
    quoteFormat.setLeftMargin(16);
    quoteFormat.setTopMargin(2);
    quoteFormat.setBottomMargin(2);
    cursor.setBlockFormat(quoteFormat);
    QTextCharFormat quoteCharFormat;
    quoteCharFormat.setFontItalic(true);
    quoteCharFormat.setForeground(QColor(0x70, 0x70, 0x70));
    cursor.insertText(quoteLines.join(QLatin1Char('\n')), quoteCharFormat);
}

bool isSpecialLine(const QString &line);

void insertTable(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i) {
    const QStringList headerCells = tableRowCells(lines.at(i));
    const QStringList alignmentsRaw = tableRowCells(lines.at(i + 1));
    i += 2;
    QStringList bodyRows;
    while (i < lines.size() && !lines.at(i).trimmed().isEmpty() &&
           tableRowCells(lines.at(i)).size() == headerCells.size() && !isSpecialLine(lines.at(i))) {
        bodyRows.push_back(lines.at(i));
        ++i;
    }
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;

    QTextTable *table = cursor.insertTable(static_cast<int>(bodyRows.size()) + 1,
                                           static_cast<int>(headerCells.size()));
    QTextTableFormat tableFormat;
    tableFormat.setBorder(1);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tableFormat.setCellSpacing(0);
    tableFormat.setCellPadding(4);
    tableFormat.setMargin(4);
    table->setFormat(tableFormat);

    QVector<Qt::Alignment> alignments;
    for (int col = 0; col < headerCells.size(); ++col)
        alignments.push_back(tableCellAlignment(alignmentsRaw.value(col)));

    QTextCharFormat headerFormat;
    headerFormat.setFontWeight(QFont::Bold);
    for (int col = 0; col < headerCells.size(); ++col) {
        const QTextTableCell cell = table->cellAt(0, col);
        QTextCursor cellCursor = cell.firstCursorPosition();
        cellCursor.insertText(headerCells.at(col), headerFormat);
    }
    for (int row = 0; row < bodyRows.size(); ++row) {
        const QStringList cells = tableRowCells(bodyRows.at(row));
        for (int col = 0; col < headerCells.size(); ++col) {
            const QTextTableCell cell = table->cellAt(row + 1, col);
            QTextCursor cellCursor = cell.firstCursorPosition();
            insertInline(cellCursor, cells.value(col));
        }
    }
    // Column alignment via the cells' block formats.
    for (int col = 0; col < headerCells.size(); ++col) {
        for (int row = 0; row <= bodyRows.size(); ++row) {
            QTextTableCell cell = table->cellAt(row, col);
            QTextCursor cellCursor = cell.firstCursorPosition();
            QTextBlockFormat cellBlock = cellCursor.blockFormat();
            cellBlock.setAlignment(alignments.at(col));
            cellCursor.setBlockFormat(cellBlock);
        }
    }
}

QTextBlockFormat listBlockFormat() {
    QTextBlockFormat listFormat;
    listFormat.setLeftMargin(16);
    listFormat.setTopMargin(1);
    listFormat.setBottomMargin(1);
    return listFormat;
}

void insertUnorderedListItem(QTextCursor &cursor, bool &firstBlock, const QString &line) {
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    cursor.setBlockFormat(listBlockFormat());
    cursor.insertText(QStringLiteral("\u2022 "));
    insertInline(cursor, line.trimmed().mid(2));
}

void insertOrderedListItem(QTextCursor &cursor, bool &firstBlock, const QString &line, int number) {
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    cursor.setBlockFormat(listBlockFormat());
    cursor.insertText(QStringLiteral("%1. ").arg(number));
    const QString trimmed = line.trimmed();
    const int dot = static_cast<int>(trimmed.indexOf(QLatin1Char('.')));
    insertInline(cursor, trimmed.mid(dot + 1).trimmed());
}

bool isSpecialLine(const QString &line) {
    int headingLevel = 0;
    int listNumber = 0;
    return isFenceLine(line) || isHeadingLine(line, &headingLevel) || isHorizontalRule(line) ||
           isBlockquoteLine(line) || isUnorderedListItem(line) ||
           isOrderedListItem(line, &listNumber);
}

void insertParagraph(QTextCursor &cursor, bool &firstBlock, const QStringList &lines, int &i) {
    QStringList paragraphLines;
    while (i < lines.size() && !lines.at(i).trimmed().isEmpty() && !isSpecialLine(lines.at(i))) {
        paragraphLines.push_back(lines.at(i));
        ++i;
    }
    if (paragraphLines.isEmpty()) {
        ++i;
        return;
    }
    if (!firstBlock)
        cursor.insertBlock();
    firstBlock = false;
    insertInline(cursor, paragraphLines.join(QLatin1Char(' ')));
}

} // namespace

void MarkdownRenderer::renderInto(QTextDocument &doc, const QString &markdown, const QFont &font) {
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    int i = 0;
    bool firstBlock = true;

    while (i < lines.size()) {
        const QString &line = lines.at(i);

        if (line.trimmed().isEmpty()) {
            ++i;
            continue;
        }
        if (isFenceLine(line)) {
            insertFencedCodeBlock(cursor, firstBlock, lines, i);
            continue;
        }
        int level = 0;
        if (isHeadingLine(line, &level)) {
            insertHeading(cursor, firstBlock, line, level, font);
            ++i;
            continue;
        }
        if (isHorizontalRule(line)) {
            insertHorizontalRule(cursor, firstBlock);
            ++i;
            continue;
        }
        if (isBlockquoteLine(line)) {
            insertBlockquote(cursor, firstBlock, lines, i);
            continue;
        }
        if (isTableStart(lines, i)) {
            insertTable(cursor, firstBlock, lines, i);
            continue;
        }
        if (isUnorderedListItem(line)) {
            insertUnorderedListItem(cursor, firstBlock, line);
            ++i;
            continue;
        }
        int number = 0;
        if (isOrderedListItem(line, &number)) {
            insertOrderedListItem(cursor, firstBlock, line, number);
            ++i;
            continue;
        }
        insertParagraph(cursor, firstBlock, lines, i);
    }

    // Remove the initial empty block if content was added after it. When
    // the first content is a table, the next block is a table cell and the
    // removal would cross the frame boundary into the table; keep the empty
    // block in that case.
    QTextCursor start(&doc);
    start.movePosition(QTextCursor::Start);
    if (start.block().text().isEmpty() && doc.blockCount() > 1) {
        QTextCursor probe(start);
        probe.movePosition(QTextCursor::NextBlock);
        if (!probe.currentTable()) {
            start.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            start.removeSelectedText();
        }
    }
}

} // namespace ocapi
