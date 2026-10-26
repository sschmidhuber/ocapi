#include "ui/chatdelegate.h"
#include "core/attachmentopener.h"
#include "core/markdown.h"
#include "core/message.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QHoverEvent>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolTip>

#include <QJsonDocument>

#include <algorithm>
#include <memory>

namespace ocapi {

namespace {

constexpr int kEdgeMargin = 12;       // margin between items and the viewport edge
constexpr int kLabelHeight = 16;      // height of the role label row above content
constexpr int kCopyButtonSize = 16;   // copy icon in the label row
constexpr int kCopyFeedbackMs = 1500; // how long the "Copied" hint stays
constexpr int kItemSpacing = 8;       // breathing room below each item
constexpr int kBubblePadding = 10;
constexpr int kBubbleRadius = 10;
constexpr int kMaxBubbleWidthFraction = 80; // percent of viewport width
constexpr int kImageSize = 320;             // max thumbnail edge in the chat
constexpr int kImageSpacing = 6;
constexpr int kImageCaptionHeight = 16; // file-name caption below an image
constexpr int kFileChipHeight = 80;     // height of a file chip in the chat
constexpr int kFileChipThumb = 64;      // thumbnail edge inside a file chip
constexpr int kFileChipPadding = 6;
constexpr int kFileChipMaxTextWidth = 320; // file name text is elided beyond this

QVector<QImage> decodeImages(const QModelIndex &index) {
    const auto attachments =
        index.data(static_cast<int>(Qt::UserRole) + 5).value<QVector<ImageAttachment>>();
    QVector<QImage> images;
    images.reserve(attachments.size());
    for (const auto &attachment : attachments)
        images.push_back(attachment.decode());
    return images;
}

QVector<FileAttachment> decodeFiles(const QModelIndex &index) {
    return index.data(static_cast<int>(Qt::UserRole) + 8).value<QVector<FileAttachment>>();
}

QVector<ImageAttachment> decodeImageAttachments(const QModelIndex &index) {
    return index.data(static_cast<int>(Qt::UserRole) + 5).value<QVector<ImageAttachment>>();
}

int fileChipWidth(const QString &fileName, const QFontMetrics &metrics) {
    const QString elided = metrics.elidedText(fileName, Qt::ElideMiddle, kFileChipMaxTextWidth);
    return (3 * kFileChipPadding) + kFileChipThumb + metrics.horizontalAdvance(elided);
}

// File chips wrap into rows within maxWidth; returns the row count.
int fileChipRowCount(const QVector<FileAttachment> &files, const QFontMetrics &metrics,
                     int maxWidth) {
    if (files.isEmpty())
        return 0;
    int rows = 1;
    int x = 0;
    for (const auto &file : files) {
        const int width = fileChipWidth(file.fileName, metrics);
        if (x > 0 && x + width > maxWidth) {
            ++rows;
            x = 0;
        }
        x += width + kImageSpacing;
    }
    return rows;
}

int filesBlockHeight(const QVector<FileAttachment> &files, const QFont &font, int maxTextWidth) {
    const int rows = fileChipRowCount(files, QFontMetrics(font), maxTextWidth);
    return rows > 0 ? (rows * kFileChipHeight) + ((rows - 1) * kImageSpacing) : 0;
}

int filesBlockWidth(const QVector<FileAttachment> &files, const QFont &font, int maxWidth) {
    if (files.isEmpty())
        return 0;
    const QFontMetrics metrics(font);
    int width = 0;
    int rowWidth = 0;
    for (const auto &file : files) {
        const int chipWidth = fileChipWidth(file.fileName, metrics);
        if (rowWidth > 0 && rowWidth + chipWidth > maxWidth) {
            width = qMax(width, rowWidth - kImageSpacing);
            rowWidth = 0;
        }
        rowWidth += chipWidth + kImageSpacing;
    }
    if (rowWidth > 0)
        width = qMax(width, rowWidth - kImageSpacing);
    return width;
}

QVector<ToolCall> decodeToolCalls(const QModelIndex &index) {
    return index.data(static_cast<int>(Qt::UserRole) + 6).value<QVector<ToolCall>>();
}

QString formatToolArguments(const QJsonObject &arguments) {
    return QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
}

// Short human-readable tool name: strip the "server__" prefix.
QString toolCallShortName(const QString &name) {
    const qsizetype sep = name.indexOf(QStringLiteral("__"));
    return sep >= 0 ? name.mid(sep + 2) : name;
}

QString toolCallHeaderText(const ToolCall &call) {
    return QStringLiteral("%1 %2").arg(QStringLiteral("\U0001F9EA0"), toolCallShortName(call.name));
}

// One-line summary of the arguments, elided to fit the header row.
QString toolCallSummary(const ToolCall &call, const QFontMetrics &metrics, int maxWidth) {
    const QString args = formatToolArguments(call.arguments);
    return metrics.elidedText(args, Qt::ElideMiddle, maxWidth);
}

// Status word shown at the right of a finished tool-call header.
QString toolCallStatusText(const ToolCall &call) {
    switch (call.status) {
    case ToolCallStatus::Done:
        return QObject::tr("done");
    case ToolCallStatus::Failed:
        return QObject::tr("failed");
    case ToolCallStatus::Running:
        return {};
    }
    return {};
}

// Height of one collapsed tool-call row (chevron + name + status).
int toolCallRowHeight(const QFont &font) {
    return qMax(kCopyButtonSize, QFontMetrics(font).height()) + 4;
}

// Height of the collapsed "Thinking" header row (chevron + label + spinner).
int reasoningRowHeight(const QFont &font) {
    return qMax(kCopyButtonSize, QFontMetrics(font).height()) + 4;
}

// Total height of the reasoning block: the header row plus the reasoning
// text (plain, elided by the text document's width) when expanded.
int reasoningBlockHeight(const QString &reasoning, bool expanded, const QFont &font,
                         int maxTextWidth) {
    if (reasoning.isEmpty() || !expanded)
        return reasoningRowHeight(font);
    QTextDocument reasoningDoc;
    reasoningDoc.setDefaultFont(font);
    reasoningDoc.setTextWidth(maxTextWidth);
    reasoningDoc.setPlainText(reasoning);
    return reasoningRowHeight(font) + reasoningDoc.size().toSize().height() + 6;
}

// Total height of the tool-call block: one row per call, plus the expanded
// result area for each expanded call.
int toolCallsBlockHeight(const QVector<ToolCall> &toolCalls, const QStringList &expandedIds,
                         const QFont &font, int maxTextWidth) {
    int height = 0;
    const int rowHeight = toolCallRowHeight(font);
    for (const auto &call : toolCalls) {
        height += rowHeight;
        if (expandedIds.contains(call.id)) {
            QTextDocument resultDoc;
            resultDoc.setDefaultFont(font);
            resultDoc.setTextWidth(maxTextWidth);
            resultDoc.setPlainText(call.result.isEmpty() ? QObject::tr("(no output)")
                                                         : call.result);
            height += resultDoc.size().toSize().height() + 6;
        }
    }
    return height;
}

int imagesBlockWidth(const QVector<QImage> &images) {
    int width = 0;
    for (const auto &image : images)
        width += qMin(image.width(), kImageSize) + kImageSpacing;
    return width > 0 ? width - kImageSpacing : 0;
}

int imagesBlockHeight(const QVector<QImage> &images) {
    int height = 0;
    for (const auto &image : images)
        height = qMax(height, qMin(image.height(), kImageSize));
    // Room for the file-name caption below each image.
    return height > 0 ? height + kImageCaptionHeight : 0;
}

// Builds the text document for a message. Assistant and tool messages are
// rendered as markdown; user messages and errors stay plain text.
std::unique_ptr<QTextDocument> messageDocument(const QString &content, bool renderMarkdown,
                                               bool streaming, const QFont &font,
                                               int maxTextWidth) {
    if (!renderMarkdown) {
        auto doc = std::make_unique<QTextDocument>();
        doc->setDefaultFont(font);
        doc->setTextWidth(maxTextWidth);
        QString displayText = content;
        if (streaming && !displayText.isEmpty())
            displayText.append(QChar(0x2588)); // block cursor while streaming
        doc->setPlainText(displayText);
        return doc;
    }
    // While streaming, the last line may be an unterminated code fence or
    // inline marker; render what is complete and show the remainder as plain
    // text so nothing is hidden from the user mid-stream.
    QString complete = content;
    QString trailing;
    int openFence = -1;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~")))
            openFence = openFence < 0 ? i : -1;
    }
    if (openFence >= 0 && streaming) {
        complete = lines.mid(0, openFence).join(QLatin1Char('\n'));
        trailing = lines.mid(openFence).join(QLatin1Char('\n'));
    }
    auto doc = MarkdownRenderer::render(complete, maxTextWidth, font);
    if (!trailing.isEmpty()) {
        QTextCursor cursor(doc.get());
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText(trailing);
    }
    if (doc->isEmpty() && content.isEmpty() && streaming)
        return doc; // reasoning spinner replaces the old placeholder
    if (streaming) {
        QTextCursor cursor(doc.get());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QChar(0x2588));
    }
    return doc;
}

// Layout shared by paint() and sizeHint() so heights always match.
struct ItemLayout {
    QRect labelRect;   // role label row (inside the item rect)
    QRect contentRect; // background/bubble area for the message content
    // Footer row below the content: copy button and throughput label.
    // Inside the bubble for user messages, plain for assistant messages.
    QRect footerRect;
    // The footer is only rendered once an assistant message has finished
    // streaming; while streaming the item height changes on every token and
    // a bottom-anchored footer would be positioned against stale geometry.
    bool hasFooter = false;
    // True for assistant messages that carry tool calls: they render as
    // collapsed elements and never get a copy button.
    bool hasToolCalls = false;
    int toolCallsHeight = 0;
    // Collapsed "Thinking" element above everything else.
    int reasoningHeight = 0;
    int imageBlockHeight = 0;
    int filesBlockHeight = 0;
    int textHeight = 0;
    QString throughput; // empty when the message has no tok/s label
};

// Layout of the attachment area shared by paint() and the click/hover
// hit-testing: image rects first (in message order), then the wrapping
// rows of file chips. Rects are in item coordinates.
struct AttachmentLayout {
    QVector<QRect> imageRects;
    QVector<QRect> fileChipRects;
};

AttachmentLayout attachmentLayout(const QVector<QImage> &images,
                                  const QVector<FileAttachment> &files,
                                  const QStyleOptionViewItem &option, const ItemLayout &layout) {
    const QRect &contentRect = layout.contentRect;
    AttachmentLayout result;
    result.imageRects.reserve(images.size());
    int x = contentRect.left() + kBubblePadding;
    const int imageTop =
        contentRect.top() + kBubblePadding + layout.reasoningHeight + layout.toolCallsHeight;
    for (const auto &image : images) {
        const int targetWidth = qMin(image.width(), kImageSize);
        const int targetHeight = qMin(image.height(), kImageSize);
        result.imageRects.push_back(QRect(x, imageTop, targetWidth, targetHeight));
        x += targetWidth + kImageSpacing;
    }

    result.fileChipRects.reserve(files.size());
    const int chipsTop = contentRect.top() + kBubblePadding + layout.reasoningHeight +
                         layout.toolCallsHeight + layout.imageBlockHeight;
    const int maxChipsWidth = contentRect.width() - (2 * kBubblePadding);
    const QFontMetrics metrics(option.font);
    const int chipsLeft = contentRect.left() + kBubblePadding;
    int chipX = chipsLeft;
    int chipY = chipsTop;
    for (const auto &file : files) {
        const int chipWidth = fileChipWidth(file.fileName, metrics);
        if (chipX > chipsLeft && chipX + chipWidth > chipsLeft + maxChipsWidth) {
            chipX = chipsLeft;
            chipY += kFileChipHeight + kImageSpacing;
        }
        result.fileChipRects.push_back(QRect(chipX, chipY, chipWidth, kFileChipHeight));
        chipX += chipWidth + kImageSpacing;
    }
    return result;
}

// Index of the attachment at \p pos, or -1. Images come first (in message
// order), then file chips. The source path identifies the file to open.
int hitTestAttachment(const QVector<ImageAttachment> &images, const QVector<FileAttachment> &files,
                      const AttachmentLayout &layout, const QPoint &pos, QString *sourcePath) {
    for (qsizetype i = 0; i < layout.imageRects.size(); ++i) {
        if (layout.imageRects.at(i).contains(pos)) {
            if (sourcePath)
                *sourcePath = images.value(i).sourcePath;
            return static_cast<int>(i);
        }
    }
    const int imageCount = static_cast<int>(layout.imageRects.size());
    for (qsizetype i = 0; i < layout.fileChipRects.size(); ++i) {
        if (layout.fileChipRects.at(i).contains(pos)) {
            if (sourcePath)
                *sourcePath = files.value(i).sourcePath;
            return imageCount + static_cast<int>(i);
        }
    }
    return -1;
}

QString throughputText(double tokensPerSecond) {
    return QStringLiteral("%1 tok/s").arg(QString::number(tokensPerSecond, 'f', 1));
}

// Font used for the label row and the message footer.
QFont labelFont(const QStyleOptionViewItem &option) {
    QFont font = option.font;
    font.setBold(true);
    font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.0));
    return font;
}

ItemLayout layoutItem(const QStyleOptionViewItem &option, const QModelIndex &index) {
    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString content = index.data(Qt::DisplayRole).toString();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    const QVector<QImage> images = decodeImages(index);
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);
    const QVector<FileAttachment> files = decodeFiles(index);

    const int availableWidth = option.rect.width() - (2 * kEdgeMargin);
    const int maxTextWidth =
        isUser ? qMax(60, (availableWidth * kMaxBubbleWidthFraction / 100) - (2 * kBubblePadding))
               : qMax(60, availableWidth);

    const QStringList expandedToolCalls =
        index.data(static_cast<int>(Qt::UserRole) + 9).toStringList();
    const QString reasoning = index.data(static_cast<int>(Qt::UserRole) + 11).toString();
    const bool reasoningExpanded = index.data(static_cast<int>(Qt::UserRole) + 14).toBool();

    ItemLayout layout;
    layout.hasToolCalls = !toolCalls.isEmpty();
    layout.reasoningHeight =
        reasoningBlockHeight(reasoning, reasoningExpanded, option.font, maxTextWidth);
    layout.toolCallsHeight =
        toolCallsBlockHeight(toolCalls, expandedToolCalls, option.font, maxTextWidth);
    layout.imageBlockHeight = images.isEmpty() ? 0 : imagesBlockHeight(images) + kImageSpacing;
    layout.filesBlockHeight =
        files.isEmpty() ? 0
                        : ocapi::filesBlockHeight(files, option.font, maxTextWidth) + kImageSpacing;

    std::unique_ptr<QTextDocument> doc;
    if (!error.isEmpty()) {
        auto errorDoc = std::make_unique<QTextDocument>();
        errorDoc->setDefaultFont(option.font);
        errorDoc->setTextWidth(maxTextWidth);
        errorDoc->setPlainText(QStringLiteral("\u26a0 ") + error);
        doc = std::move(errorDoc);
    } else {
        doc = messageDocument(content, !isUser, streaming, option.font, maxTextWidth);
    }
    layout.textHeight = doc->size().toSize().height();

    const int textWidth = doc->size().toSize().width();
    const int imageBlockWidth = imagesBlockWidth(images);
    const int fileBlockWidth = filesBlockWidth(files, option.font, maxTextWidth);
    const int contentWidth = qMax(textWidth, qMax(qMin(imageBlockWidth, maxTextWidth),
                                                  qMin(fileBlockWidth, maxTextWidth))) +
                             (2 * kBubblePadding);
    const int contentHeight = layout.textHeight + layout.imageBlockHeight +
                              layout.filesBlockHeight + layout.toolCallsHeight +
                              layout.reasoningHeight + (2 * kBubblePadding);

    const double tokensPerSecond = index.data(static_cast<int>(Qt::UserRole) + 4).toDouble();
    layout.throughput = tokensPerSecond > 0.0 ? throughputText(tokensPerSecond) : QString();

    const int labelTop = option.rect.top();
    const int contentTop = labelTop + kLabelHeight + 2;

    layout.labelRect = QRect(option.rect.left() + kEdgeMargin, labelTop,
                             option.rect.width() - (2 * kEdgeMargin), kLabelHeight);
    if (isUser) {
        // The footer (copy button, throughput) sits inside the bubble, at
        // its bottom left; the bubble grows to include it.
        layout.hasFooter = true;
        QRect bubbleRect(option.rect.right() - kEdgeMargin - contentWidth, contentTop, contentWidth,
                         contentHeight + kLabelHeight + kBubblePadding);
        layout.contentRect = bubbleRect;
        layout.footerRect = QRect(bubbleRect.left() + kBubblePadding,
                                  bubbleRect.bottom() - kBubblePadding - kLabelHeight + 1,
                                  bubbleRect.width() - (2 * kBubblePadding), kLabelHeight);
    } else {
        // Assistant/tool/error messages use the full chat area: no bubble.
        // The footer appears only once the message no longer streams, and
        // never for messages that carry tool calls (those render as
        // collapsed elements without a copy button).
        layout.contentRect =
            QRect(option.rect.left() + kEdgeMargin, contentTop, availableWidth, contentHeight);
        layout.hasFooter = !streaming && !layout.hasToolCalls;
        if (layout.hasFooter)
            layout.footerRect =
                QRect(option.rect.left() + kEdgeMargin, layout.contentRect.bottom() + 2,
                      availableWidth, kLabelHeight);
    }
    return layout;
}

// Index of the attachment at \p pos (item coordinates), or -1; shared by
// the delegate's hover tracking, click handling, tooltip and the view's
// cursor switching.
int attachmentIndexAt(const QStyleOptionViewItem &option, const QModelIndex &index,
                      const QPoint &pos, QString *sourcePath = nullptr) {
    const QVector<ImageAttachment> images = decodeImageAttachments(index);
    const QVector<FileAttachment> files = decodeFiles(index);
    if (images.isEmpty() && files.isEmpty())
        return -1;
    const ItemLayout layout = layoutItem(option, index);
    const QVector<QImage> decoded = decodeImages(index);
    const AttachmentLayout attachments = attachmentLayout(decoded, files, option, layout);
    return hitTestAttachment(images, files, attachments, pos, sourcePath);
}

const QIcon &copyIcon() {
    static const QIcon icon(QString::fromUtf8(":/resources/icons/edit-copy-symbolic.svg"));
    return icon;
}

// Rect of the per-message copy button inside the footer row, at its left.
QRect copyButtonRect(const QRect &footerRect) {
    QRect rect(0, 0, kCopyButtonSize, kCopyButtonSize);
    rect.moveCenter(footerRect.center());
    rect.moveLeft(footerRect.left());
    return rect;
}

// Paints one file chip: a rounded tile with the file thumbnail and its
// (elided) name.
void paintFileChip(QPainter *painter, const QStyleOptionViewItem &option,
                   const FileAttachment &file, const QRect &chipRect, bool isUser) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 0x0d));
    painter->drawRoundedRect(chipRect, kBubbleRadius / 2.0, kBubbleRadius / 2.0);

    const QRect thumbRect(chipRect.left() + kFileChipPadding,
                          chipRect.top() + ((kFileChipHeight - kFileChipThumb) / 2), kFileChipThumb,
                          kFileChipThumb);
    const QImage thumb = file.decodeThumbnail();
    if (!thumb.isNull()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::white);
        painter->drawRoundedRect(thumbRect, 3, 3);
        painter->drawImage(thumbRect, thumb.scaled(kFileChipThumb, kFileChipThumb,
                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x75, 0x75, 0x75));
        painter->drawRoundedRect(thumbRect, 3, 3);
        painter->setPen(Qt::white);
        painter->setFont(option.font);
        painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("?"));
    }

    painter->setPen(isUser ? Qt::black : QColor(0x1a, 0x1a, 0x1a));
    painter->setFont(option.font);
    const QFontMetrics metrics(option.font);
    const QString elided =
        metrics.elidedText(file.fileName, Qt::ElideMiddle, kFileChipMaxTextWidth);
    const QRect labelRect(thumbRect.right() + kFileChipPadding, chipRect.top(),
                          chipRect.right() - kFileChipPadding - thumbRect.right() -
                              kFileChipPadding,
                          chipRect.height());
    painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
}

// Paints the file chips below the images, above the text, using the
// precomputed rects from attachmentLayout(). \p hoveredChip highlights the
// chip under the mouse (it opens on click); -1 highlights none.
void paintFileChips(QPainter *painter, const QStyleOptionViewItem &option,
                    const QVector<FileAttachment> &files, const QVector<QRect> &chipRects,
                    bool isUser, int hoveredChip) {
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QRect chipRect = chipRects.value(i);
        paintFileChip(painter, option, files.at(i), chipRect, isUser);
        if (i == hoveredChip) {
            painter->setPen(QPen(QColor(0x2a, 0x62, 0xb5), 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(chipRect, kBubbleRadius / 2.0, kBubbleRadius / 2.0);
        }
    }
}

// Spinner phase (degrees) for the animated progress indicators; derived
// from elapsed time so every repaint advances the rotation. Qt arc angles
// are counter-clockwise, so the phase is negated to spin clockwise.
int spinnerPhase() {
    return -static_cast<int>((QDateTime::currentMSecsSinceEpoch() / 8) % 360);
}

// Paints a spinning arc into \p rect with the given rotation phase.
void paintSpinner(QPainter *painter, const QRect &rect, int phase) {
    painter->setPen(QPen(QColor(0x2a, 0x62, 0xb5), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(rect, (phase + 45) * 16, 270 * 16);
}

// True when the painted item needs a spinner: reasoning tokens still
// streaming or a tool call still running.
bool spinnerActive(const QModelIndex &index, const QVector<ToolCall> &toolCalls) {
    if (index.data(static_cast<int>(Qt::UserRole) + 12).toBool())
        return true;
    return std::ranges::any_of(
        toolCalls, [](const ToolCall &call) { return call.status == ToolCallStatus::Running; });
}

// Paints the collapsed "Thinking" element above the message text: a chevron,
// the "Thinking" label, an animated spinner next to the label while
// reasoning tokens are still arriving, and the reasoning text below the
// header when expanded.
void paintReasoning(QPainter *painter, const QStyleOptionViewItem &option, const QString &reasoning,
                    bool reasoningStreaming, bool expanded, const QRect &contentRect,
                    int maxTextWidth) {
    if (reasoning.isEmpty())
        return;
    const int rowHeight = reasoningRowHeight(option.font);
    const QColor rowColor(0x50, 0x50, 0x50);
    const QColor expandedBg(0, 0, 0, 0x0d);
    const QRect row(contentRect.left() + kBubblePadding, contentRect.top() + kBubblePadding,
                    contentRect.width(), rowHeight);

    painter->setPen(QPen(rowColor, 1.5));
    const int chevX = row.left() + 2;
    const int chevY = row.top() + (row.height() / 2);
    if (expanded) {
        painter->drawLine(chevX, chevY - 4, chevX + 4, chevY + 1);
        painter->drawLine(chevX + 4, chevY + 1, chevX + 8, chevY - 4);
    } else {
        painter->drawLine(chevX, chevY - 4, chevX + 5, chevY);
        painter->drawLine(chevX + 5, chevY, chevX, chevY + 4);
    }

    QFont label = option.font;
    label.setItalic(true);
    painter->setFont(label);
    painter->setPen(QColor(0x1a, 0x1a, 0x1a));
    const QString labelText = QObject::tr("Thinking");
    painter->drawText(QRect(row.left() + 16, row.top(),
                            QFontMetrics(label).horizontalAdvance(labelText), row.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, labelText);

    // Animated progress indicator directly next to the "Thinking" label
    // while the reasoning tokens stream in.
    if (reasoningStreaming) {
        const int spinnerSize = kCopyButtonSize;
        const QRect spinnerRect(
            row.left() + 16 + QFontMetrics(label).horizontalAdvance(labelText) + 6,
            row.top() + ((row.height() - spinnerSize) / 2), spinnerSize, spinnerSize);
        paintSpinner(painter, spinnerRect, spinnerPhase());
    }

    if (!expanded)
        return;
    QTextDocument reasoningDoc;
    reasoningDoc.setDefaultFont(option.font);
    reasoningDoc.setTextWidth(maxTextWidth);
    reasoningDoc.setPlainText(reasoning);
    const int reasoningTextHeight = reasoningDoc.size().toSize().height();
    const QRect textRect(row.left(), row.bottom() + 6, row.width(), reasoningTextHeight + 6);
    painter->setPen(Qt::NoPen);
    painter->setBrush(expandedBg);
    painter->drawRoundedRect(textRect, 4, 4);
    painter->setFont(option.font);
    painter->setPen(rowColor);
    painter->save();
    painter->translate(row.left() + 6, row.bottom() + 9);
    reasoningDoc.drawContents(painter);
    painter->restore();
}

// Paints the collapsed tool-call elements above the message text: one row
// per call with a chevron, the tool name and a status/spinner indicator;
// expanded calls additionally show their result text below the row.
void paintToolCalls(QPainter *painter, const QStyleOptionViewItem &option,
                    const QVector<ToolCall> &toolCalls, const QStringList &expandedIds,
                    const QRect &contentRect, int maxTextWidth) {
    if (toolCalls.isEmpty())
        return;
    const int rowHeight = toolCallRowHeight(option.font);
    const QColor rowColor(0x50, 0x50, 0x50);
    const QColor expandedBg(0, 0, 0, 0x0d);
    int y = contentRect.top() + kBubblePadding;
    for (const auto &call : toolCalls) {
        const bool expanded = expandedIds.contains(call.id);
        const QRect row(contentRect.left() + kBubblePadding, y, contentRect.width(), rowHeight);

        // Chevron: right-pointing when collapsed, down when expanded.
        painter->setPen(QPen(rowColor, 1.5));
        const int chevX = row.left() + 2;
        const int chevY = row.top() + (row.height() / 2);
        if (expanded) {
            painter->drawLine(chevX, chevY - 4, chevX + 4, chevY + 1);
            painter->drawLine(chevX + 4, chevY + 1, chevX + 8, chevY - 4);
        } else {
            painter->drawLine(chevX, chevY - 4, chevX + 5, chevY);
            painter->drawLine(chevX + 5, chevY, chevX, chevY + 4);
        }

        // Tool name in bold, then the argument summary in regular gray.
        QFont nameFont = option.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        painter->setPen(QColor(0x1a, 0x1a, 0x1a));
        const QFontMetrics nameMetrics(nameFont);
        const QString name = toolCallHeaderText(call);
        const int nameWidth = nameMetrics.horizontalAdvance(name);
        painter->drawText(QRect(row.left() + 16, row.top(), nameWidth, row.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, name);

        painter->setFont(option.font);
        painter->setPen(rowColor);
        const QFontMetrics metrics(option.font);
        const int summaryMax = qMax(40, maxTextWidth - 16 - nameWidth - 60);
        const QString summary = toolCallSummary(call, metrics, summaryMax);
        painter->drawText(QRect(row.left() + 16 + nameWidth + 6, row.top(),
                                row.right() - (row.left() + 16 + nameWidth + 6) - 60, row.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, summary);

        // Status at the right edge: animated spinner while running, status
        // word when finished.
        if (call.status == ToolCallStatus::Running) {
            const int spinnerSize = kCopyButtonSize;
            const QRect spinnerRect(row.right() - spinnerSize - 2,
                                    row.top() + ((row.height() - spinnerSize) / 2), spinnerSize,
                                    spinnerSize);
            paintSpinner(painter, spinnerRect, spinnerPhase());
        } else {
            const QString status = toolCallStatusText(call);
            if (!status.isEmpty()) {
                painter->setPen(call.status == ToolCallStatus::Failed ? QColor(0xb0, 0x30, 0x30)
                                                                      : QColor(0x2e, 0x7d, 0x32));
                painter->drawText(row, Qt::AlignRight | Qt::AlignVCenter, status);
            }
        }

        y += rowHeight;
        if (expanded) {
            QTextDocument resultDoc;
            resultDoc.setDefaultFont(option.font);
            resultDoc.setTextWidth(maxTextWidth);
            resultDoc.setPlainText(call.result.isEmpty() ? QObject::tr("(no output)")
                                                         : call.result);
            const int resultHeight = resultDoc.size().toSize().height();
            const QRect resultRect(row.left(), y, row.width(), resultHeight + 6);
            painter->setPen(Qt::NoPen);
            painter->setBrush(expandedBg);
            painter->drawRoundedRect(resultRect, 4, 4);
            painter->translate(row.left() + 6, y + 3);
            resultDoc.drawContents(painter);
            painter->translate(-(row.left() + 6), -(y + 3));
            y += resultHeight + 6;
        }
    }
}

// Paints the message footer: copy button and throughput label at the left
// of the footer row, plus the transient "Copied" hint. For user messages
// the footer sits inside the bubble (light colors); for assistant messages
// it sits below the content (dark colors).
void paintFooterRow(QPainter *painter, const QStyleOptionViewItem &option, const ItemLayout &layout,
                    bool isUser, bool showCopied) {
    if (!layout.hasFooter)
        return;
    const QRect footerRect = layout.footerRect;
    const QColor labelColor = QColor(0x50, 0x50, 0x50);
    painter->setPen(labelColor);
    painter->setFont(labelFont(option));

    const QRect copyRect = copyButtonRect(footerRect);
    copyIcon().paint(painter, copyRect);
    const int hintWidth = QFontMetrics(labelFont(option)).horizontalAdvance(QObject::tr("Copied"));
    int x = copyRect.right() + 6;
    if (showCopied) {
        painter->setPen(QColor(0x2a, 0x62, 0xb5));
        painter->drawText(QRect(x, footerRect.top(), hintWidth, footerRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Copied"));
        x += hintWidth + 6;
    }
    if (!layout.throughput.isEmpty())
        painter->drawText(
            QRect(x, footerRect.top(), footerRect.right() - x + 1, footerRect.height()),
            Qt::AlignLeft | Qt::AlignVCenter, layout.throughput);
}

// Handles a click on a message that carries tool calls: toggles the
// expansion of the clicked row. Returns true when the click was consumed.
bool handleToolCallClick(QAbstractItemModel *model, const QModelIndex &index,
                         const QStyleOptionViewItem &option, const ItemLayout &layout,
                         const QPoint &pos) {
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);
    if (toolCalls.isEmpty())
        return false;
    const QStringList expanded = index.data(static_cast<int>(Qt::UserRole) + 9).toStringList();
    const int rowHeight = toolCallRowHeight(option.font);
    const int blockTop = layout.contentRect.top() + kBubblePadding + layout.reasoningHeight;
    const int clickY = pos.y() - blockTop;
    if (clickY < 0 || clickY >= layout.toolCallsHeight)
        return false;
    // Find which row was clicked, accounting for expanded results.
    int y = 0;
    for (const auto &call : toolCalls) {
        if (clickY >= y && clickY < y + rowHeight) {
            model->setData(index, call.id, static_cast<int>(Qt::UserRole) + 10);
            return true;
        }
        y += rowHeight;
        if (expanded.contains(call.id)) {
            const int maxTextWidth = layout.contentRect.width() - (2 * kBubblePadding);
            QTextDocument resultDoc;
            resultDoc.setDefaultFont(option.font);
            resultDoc.setTextWidth(maxTextWidth);
            resultDoc.setPlainText(call.result.isEmpty() ? QObject::tr("(no output)")
                                                         : call.result);
            y += resultDoc.size().toSize().height() + 6;
        }
    }
    return false;
}

// Handles a click on the collapsed "Thinking" header: toggles the element's
// expansion. Returns true when the click was consumed.
bool handleReasoningClick(QAbstractItemModel *model, const QModelIndex &index,
                          const QStyleOptionViewItem &option, const ItemLayout &layout,
                          const QPoint &pos) {
    const QString reasoning = index.data(static_cast<int>(Qt::UserRole) + 11).toString();
    if (reasoning.isEmpty() || layout.reasoningHeight <= 0)
        return false;
    const int rowHeight = reasoningRowHeight(option.font);
    const QRect row(layout.contentRect.left() + kBubblePadding,
                    layout.contentRect.top() + kBubblePadding, layout.contentRect.width(),
                    rowHeight);
    if (!row.contains(pos))
        return false;
    model->setData(index, {}, static_cast<int>(Qt::UserRole) + 13);
    return true;
}

} // namespace

ChatDelegate::ChatDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    m_copiedTimer.setSingleShot(true);
    connect(&m_copiedTimer, &QTimer::timeout, this, [this] {
        m_copiedIndex = QPersistentModelIndex();
        emit copiedFeedbackChanged();
    });
    m_spinnerTimer.setInterval(16);
    connect(&m_spinnerTimer, &QTimer::timeout, this, &ChatDelegate::spinnerAnimationChanged);
}

void ChatDelegate::updateSpinnerAnimation(bool spinnerVisible) const {
    if (m_spinnerVisible == spinnerVisible)
        return;
    m_spinnerVisible = spinnerVisible;
    if (spinnerVisible)
        m_spinnerTimer.start();
    else
        m_spinnerTimer.stop();
}

void ChatDelegate::showCopiedFeedback(const QModelIndex &index) {
    m_copiedIndex = QPersistentModelIndex(index);
    m_copiedTimer.start(kCopyFeedbackMs);
    emit copiedFeedbackChanged();
}

void ChatDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(option.rect);

    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const QString roleLabel = index.data(static_cast<int>(Qt::UserRole) + 1).toString();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString content = index.data(Qt::DisplayRole).toString();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    const QVector<QImage> images = decodeImages(index);
    const auto imageAttachments =
        index.data(static_cast<int>(Qt::UserRole) + 5).value<QVector<ImageAttachment>>();
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);

    const ItemLayout layout = layoutItem(option, index);
    const QRect contentRect = layout.contentRect;
    const int maxTextWidth = contentRect.width() - (2 * kBubblePadding);
    const QVector<FileAttachment> files = decodeFiles(index);

    // The chat area is white (see ChatView); use fixed dark text colors so
    // messages stay readable regardless of the widget theme.
    QColor textColor;
    if (!error.isEmpty()) {
        textColor = QColor(0xf6, 0xc2, 0xc2);
    } else if (isUser) {
        textColor = Qt::black;
    } else {
        textColor = QColor(0x1a, 0x1a, 0x1a);
    }

    std::unique_ptr<QTextDocument> doc;
    if (!error.isEmpty()) {
        doc = std::make_unique<QTextDocument>();
        doc->setTextWidth(maxTextWidth);
        QFont errorFont = option.font;
        errorFont.setItalic(true);
        doc->setDefaultFont(errorFont);
        doc->setPlainText(QStringLiteral("\u26a0 ") + error);
    } else {
        doc = messageDocument(content, !isUser, streaming, option.font, maxTextWidth);
    }
    // Text uses the message text color; markdown inline formats (code
    // background, link color) are kept.
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat existing = it.fragment().charFormat();
            if (existing.isAnchor() || existing.background().style() != Qt::NoBrush)
                continue;
            QTextCursor fragmentCursor(block);
            fragmentCursor.setPosition(it.fragment().position());
            fragmentCursor.setPosition(it.fragment().position() + it.fragment().length(),
                                       QTextCursor::KeepAnchor);
            QTextCharFormat colorFormat;
            colorFormat.setForeground(textColor);
            fragmentCursor.mergeCharFormat(colorFormat);
        }
    }

    // Label row above the content: just the role label.
    painter->setPen(QColor(0x50, 0x50, 0x50));
    painter->setFont(labelFont(option));
    if (isUser)
        painter->drawText(layout.labelRect, Qt::AlignRight | Qt::AlignVCenter, roleLabel);
    else
        painter->drawText(layout.labelRect, Qt::AlignLeft | Qt::AlignVCenter, roleLabel);

    // User messages keep a highlight bubble; assistant/tool/error messages
    // use the full chat area without a background.
    if (isUser) {
        QColor bubbleColor(0xc2, 0xe0, 0xf5);
        if (!error.isEmpty())
            bubbleColor = QColor(0x5a, 0x1d, 0x1d);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bubbleColor);
        painter->drawRoundedRect(contentRect, kBubbleRadius, kBubbleRadius);
    } else if (!error.isEmpty()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x5a, 0x1d, 0x1d));
        painter->drawRoundedRect(contentRect, kBubbleRadius, kBubbleRadius);
    }

    // Reasoning ("thinking") tokens above everything else, in a collapsed
    // element with a progress indicator while the tokens are streaming.
    const QString reasoning = index.data(static_cast<int>(Qt::UserRole) + 11).toString();
    const bool reasoningStreaming = index.data(static_cast<int>(Qt::UserRole) + 12).toBool();
    const bool reasoningExpanded = index.data(static_cast<int>(Qt::UserRole) + 14).toBool();
    paintReasoning(painter, option, reasoning, reasoningStreaming, reasoningExpanded, contentRect,
                   maxTextWidth);

    // Tool calls requested by the assistant, above the message text, as
    // collapsed rows (expandable, with a progress indicator while running).
    const QStringList expandedToolCalls =
        index.data(static_cast<int>(Qt::UserRole) + 9).toStringList();
    paintToolCalls(painter, option, toolCalls, expandedToolCalls,
                   contentRect.adjusted(0, layout.reasoningHeight, 0, layout.reasoningHeight),
                   maxTextWidth);

    // Keep the animation timer running while this item shows a spinner.
    updateSpinnerAnimation(spinnerActive(index, toolCalls));

    // Images above the message text, each with its file name below. The
    // rects come from the shared attachment layout so hit-testing matches
    // the painted geometry exactly.
    const AttachmentLayout attachments = attachmentLayout(images, files, option, layout);
    const QFontMetrics captionMetrics(option.font);
    for (qsizetype i = 0; i < images.size(); ++i) {
        const QImage &image = images.at(i);
        const QRect target = attachments.imageRects.at(i);
        painter->drawImage(target, image);
        // Highlight the attachment under the mouse: it opens on click.
        if (m_hoveredIndex == index && m_hoveredAttachment == i) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 0x1a));
            painter->drawRoundedRect(target, 4, 4);
        }
        const QString fileName = imageAttachments.value(i).fileName;
        if (!fileName.isEmpty()) {
            painter->setFont(option.font);
            painter->setPen(textColor);
            painter->drawText(QRect(target.left(), target.top() + target.height(), target.width(),
                                    kImageCaptionHeight),
                              Qt::AlignHCenter | Qt::AlignTop,
                              captionMetrics.elidedText(fileName, Qt::ElideMiddle, target.width()));
        }
    }

    // File chips below the images, above the message text: rounded tiles
    // with the file thumbnails and their (elided) names.
    const int imageCount = static_cast<int>(images.size());
    paintFileChips(painter, option, files, attachments.fileChipRects, isUser,
                   m_hoveredIndex == index ? m_hoveredAttachment - imageCount : -1);

    // Message text.
    painter->setPen(textColor);
    painter->translate(contentRect.left() + kBubblePadding,
                       (contentRect.top() + kBubblePadding + layout.reasoningHeight +
                        layout.toolCallsHeight + layout.imageBlockHeight +
                        layout.filesBlockHeight));
    doc->drawContents(painter);
    painter->translate(-(contentRect.left() + kBubblePadding),
                       -(contentRect.top() + kBubblePadding + layout.reasoningHeight +
                         layout.toolCallsHeight + layout.imageBlockHeight +
                         layout.filesBlockHeight));

    // Footer below the content: copy button and throughput label.
    paintFooterRow(painter, option, layout, isUser, m_copiedIndex == index);

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const ItemLayout layout = layoutItem(option, index);
    // User footers live inside the bubble (already counted); assistant
    // footers add a row below the content, but only once streaming ended.
    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const int footerHeight = (!isUser && layout.hasFooter) ? kLabelHeight + 2 : 0;
    const int height = (layout.contentRect.top() - option.rect.top()) +
                       layout.contentRect.height() + footerHeight + kItemSpacing;
    return {option.rect.width(), height};
}

bool ChatDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                               const QStyleOptionViewItem &option, const QModelIndex &index) {
    switch (event->type()) {
    case QEvent::MouseButtonRelease:
        return handleMouseRelease(event, model, option, index);
    case QEvent::HoverMove: {
        // Track the hovered attachment so paint() can highlight it. Hover
        // events reach the delegate because the view enables WA_Hover on its
        // viewport.
        const auto *hover = dynamic_cast<const QHoverEvent *>(event);
        if (!hover)
            break;
        setHoveredAttachment(index, attachmentIndexAt(option, index, hover->position().toPoint()));
        return false;
    }
    case QEvent::HoverLeave:
        setHoveredAttachment(index, -1);
        return false;
    default:
        break;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool ChatDelegate::handleMouseRelease(QEvent *event, QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) {
    const auto *mouse = dynamic_cast<const QMouseEvent *>(event);
    if (!mouse)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const ItemLayout layout = layoutItem(option, index);
    // The "Thinking" header toggles the reasoning element.
    if (handleReasoningClick(model, index, option, layout, mouse->pos()))
        return true;
    // Tool-call rows toggle the collapsed element instead of copying.
    if (handleToolCallClick(model, index, option, layout, mouse->pos()))
        return true;
    // Clicking an image or file chip opens the attachment with the
    // system's default application.
    QString sourcePath;
    if (attachmentIndexAt(option, index, mouse->pos(), &sourcePath) >= 0 && !sourcePath.isEmpty()) {
        QString openError;
        if (!AttachmentOpener::open(sourcePath, &openError))
            emit attachmentOpenFailed(openError);
        return true;
    }
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);
    if (!toolCalls.isEmpty())
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    if (!layout.hasFooter)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    if (!copyButtonRect(layout.footerRect).contains(mouse->pos()))
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    QString text = index.data(Qt::DisplayRole).toString();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    if (!error.isEmpty())
        text = error;
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        showCopiedFeedback(index);
    }
    return true;
}

bool ChatDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                             const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (event->type() != QEvent::ToolTip)
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    const QVector<ImageAttachment> images = decodeImageAttachments(index);
    const QVector<FileAttachment> files = decodeFiles(index);
    if (images.isEmpty() && files.isEmpty())
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    QString sourcePath;
    const int hit = attachmentIndexAt(option, index, event->pos(), &sourcePath);
    if (hit < 0)
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    QString name;
    const int imageCount = static_cast<int>(images.size());
    if (hit < imageCount)
        name = images.value(hit).fileName;
    else
        name = files.value(hit - imageCount).fileName;
    if (name.isEmpty())
        name = QFileInfo(sourcePath).fileName();
    QToolTip::showText(event->globalPos(), tr("%1\nClick to open").arg(name), view->viewport());
    return true;
}

bool ChatDelegate::attachmentAt(const QModelIndex &index, const QRect &itemRect,
                                const QPoint &pos) {
    QStyleOptionViewItem option;
    option.rect = itemRect;
    option.font = QGuiApplication::font();
    return attachmentIndexAt(option, index, pos) >= 0;
}

void ChatDelegate::setHoveredAttachment(const QModelIndex &index, int attachmentIndex) {
    if (m_hoveredIndex == index && m_hoveredAttachment == attachmentIndex)
        return;
    m_hoveredIndex = QPersistentModelIndex(index);
    m_hoveredAttachment = attachmentIndex;
    emit hoverChanged();
}

} // namespace ocapi
