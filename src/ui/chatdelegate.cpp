#include "ui/chatdelegate.h"
#include "core/markdown.h"
#include "core/message.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <QJsonDocument>

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
constexpr int kImageSize = 160;             // max thumbnail edge in the chat
constexpr int kImageSpacing = 6;
constexpr int kFileChipHeight = 40; // height of a file chip in the chat
constexpr int kFileChipThumb = 32;  // thumbnail edge inside a file chip
constexpr int kFileChipPadding = 6;
constexpr int kFileChipMaxTextWidth = 160; // file name text is elided beyond this

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

QString toolCallDisplayText(const ToolCall &call) {
    return QStringLiteral("\U0001F9EA0 %1(%2)").arg(call.name, formatToolArguments(call.arguments));
}

int toolCallsBlockHeight(const QVector<ToolCall> &toolCalls, const QFont &font, int maxTextWidth) {
    int height = 0;
    for (const auto &call : toolCalls) {
        QTextDocument callDoc;
        callDoc.setDefaultFont(font);
        callDoc.setTextWidth(maxTextWidth);
        callDoc.setPlainText(toolCallDisplayText(call));
        height += callDoc.size().toSize().height() + 2;
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
    return height;
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
        doc->setPlainText(displayText.isEmpty() && streaming ? QStringLiteral("\u2026")
                                                             : displayText);
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
    if (doc->isEmpty() && content.isEmpty() && streaming) {
        doc->setPlainText(QStringLiteral("\u2026"));
        return doc;
    }
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
    int toolCallsHeight = 0;
    int imageBlockHeight = 0;
    int filesBlockHeight = 0;
    int textHeight = 0;
    QString throughput; // empty when the message has no tok/s label
};

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

    ItemLayout layout;
    layout.toolCallsHeight = toolCallsBlockHeight(toolCalls, option.font, maxTextWidth);
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
                              (2 * kBubblePadding);

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
                         contentHeight + kLabelHeight);
        layout.contentRect = bubbleRect;
        layout.footerRect =
            QRect(bubbleRect.left() + kBubblePadding, bubbleRect.bottom() - kLabelHeight + 1,
                  bubbleRect.width() - (2 * kBubblePadding), kLabelHeight);
    } else {
        // Assistant/tool/error messages use the full chat area: no bubble.
        // The footer appears only once the message no longer streams.
        layout.contentRect =
            QRect(option.rect.left() + kEdgeMargin, contentTop, availableWidth, contentHeight);
        layout.hasFooter = !streaming;
        if (layout.hasFooter)
            layout.footerRect =
                QRect(option.rect.left() + kEdgeMargin, layout.contentRect.bottom() + 2,
                      availableWidth, kLabelHeight);
    }
    return layout;
}

const QIcon &copyIcon() {
    static const QIcon icon =
        QIcon::fromTheme(QStringLiteral("edit-copy-symbolic"),
                         QIcon(QString::fromUtf8(":/resources/icons/edit-copy-symbolic.svg")));
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
// (elided) name. Returns the x position right after the chip.
int paintFileChip(QPainter *painter, const QStyleOptionViewItem &option, const FileAttachment &file,
                  const QRect &chipRect, bool isUser) {
    painter->setPen(QPen(QColor(0, 0, 0, 40), 1));
    painter->setBrush(isUser ? QColor(0xff, 0xff, 0xff, 0x2e) : QColor(0, 0, 0, 0x0d));
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

    painter->setPen(isUser ? Qt::white : QColor(0x1a, 0x1a, 0x1a));
    painter->setFont(option.font);
    const QFontMetrics metrics(option.font);
    const QString elided =
        metrics.elidedText(file.fileName, Qt::ElideMiddle, kFileChipMaxTextWidth);
    const QRect labelRect(thumbRect.right() + kFileChipPadding, chipRect.top(),
                          chipRect.right() - kFileChipPadding - thumbRect.right() -
                              kFileChipPadding,
                          chipRect.height());
    painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    return chipRect.right() + kImageSpacing;
}

// Paints the wrapping row of file chips below the images, above the text.
void paintFileChips(QPainter *painter, const QStyleOptionViewItem &option,
                    const QVector<FileAttachment> &files, const QRect &contentRect,
                    const ItemLayout &layout, bool isUser) {
    if (files.isEmpty())
        return;
    const int chipsTop =
        contentRect.top() + kBubblePadding + layout.toolCallsHeight + layout.imageBlockHeight;
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
        chipX = paintFileChip(painter, option, file,
                              QRect(chipX, chipY, chipWidth, kFileChipHeight), isUser);
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
    const QColor labelColor = isUser ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x50, 0x50, 0x50);
    painter->setPen(labelColor);
    painter->setFont(labelFont(option));

    const QRect copyRect = copyButtonRect(footerRect);
    copyIcon().paint(painter, copyRect);
    const int hintWidth = QFontMetrics(labelFont(option)).horizontalAdvance(QObject::tr("Copied"));
    int x = copyRect.right() + 6;
    if (showCopied) {
        painter->setPen(QColor(0x9a, 0xd1, 0xff));
        painter->drawText(QRect(x, footerRect.top(), hintWidth, footerRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Copied"));
        x += hintWidth + 6;
    }
    if (!layout.throughput.isEmpty())
        painter->drawText(
            QRect(x, footerRect.top(), footerRect.right() - x + 1, footerRect.height()),
            Qt::AlignLeft | Qt::AlignVCenter, layout.throughput);
}

} // namespace

ChatDelegate::ChatDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    m_copiedTimer.setSingleShot(true);
    connect(&m_copiedTimer, &QTimer::timeout, this, [this] {
        m_copiedIndex = QPersistentModelIndex();
        emit copiedFeedbackChanged();
    });
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
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);
    const bool isToolMessage = roleLabel == QStringLiteral("Tool");
    const QString toolName = index.data(static_cast<int>(Qt::UserRole) + 7).toString();

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
        textColor = Qt::white;
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
    if (isToolMessage && !toolName.isEmpty()) {
        QTextCursor cursor(doc.get());
        cursor.movePosition(QTextCursor::Start);
        QTextCharFormat toolFormat;
        toolFormat.setFontItalic(true);
        cursor.insertText(QStringLiteral("\U0001F9EA0 %1\n").arg(toolName), toolFormat);
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
        QColor bubbleColor(0x2a, 0x62, 0xb5);
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

    // Tool calls requested by the assistant, above the message text.
    int toolY = contentRect.top() + kBubblePadding;
    QFont toolFont = option.font;
    toolFont.setItalic(true);
    painter->setFont(toolFont);
    painter->setPen(QColor(0x50, 0x50, 0x50));
    for (const auto &call : toolCalls) {
        QTextDocument callDoc;
        callDoc.setDefaultFont(toolFont);
        callDoc.setTextWidth(maxTextWidth);
        callDoc.setPlainText(toolCallDisplayText(call));
        const int callHeight = callDoc.size().toSize().height();
        painter->translate(contentRect.left() + kBubblePadding, toolY);
        callDoc.drawContents(painter);
        painter->translate(-(contentRect.left() + kBubblePadding), -toolY);
        toolY += callHeight + 2;
    }

    // Images above the message text.
    int x = contentRect.left() + kBubblePadding;
    const int imageTop = contentRect.top() + kBubblePadding + layout.toolCallsHeight;
    for (const auto &image : images) {
        const int targetWidth = qMin(image.width(), kImageSize);
        const int targetHeight = qMin(image.height(), kImageSize);
        const QRect target(x, imageTop, targetWidth, targetHeight);
        painter->drawImage(target, image);
        x += targetWidth + kImageSpacing;
    }

    // File chips below the images, above the message text: rounded tiles
    // with the file thumbnails and their (elided) names.
    paintFileChips(painter, option, files, contentRect, layout, isUser);

    // Message text.
    painter->setPen(textColor);
    painter->translate(contentRect.left() + kBubblePadding,
                       (contentRect.top() + kBubblePadding + layout.toolCallsHeight +
                        layout.imageBlockHeight + layout.filesBlockHeight));
    doc->drawContents(painter);
    painter->translate(-(contentRect.left() + kBubblePadding),
                       -(contentRect.top() + kBubblePadding + layout.toolCallsHeight +
                         layout.imageBlockHeight + layout.filesBlockHeight));

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
    if (event->type() != QEvent::MouseButtonRelease)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const auto *mouse = dynamic_cast<const QMouseEvent *>(event);
    if (!mouse)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const ItemLayout layout = layoutItem(option, index);
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

} // namespace ocapi
