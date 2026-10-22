#include "ui/chatdelegate.h"
#include "core/markdown.h"
#include "core/message.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
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

QVector<QImage> decodeImages(const QModelIndex &index) {
    const auto attachments =
        index.data(static_cast<int>(Qt::UserRole) + 5).value<QVector<ImageAttachment>>();
    QVector<QImage> images;
    images.reserve(attachments.size());
    for (const auto &attachment : attachments)
        images.push_back(attachment.decode());
    return images;
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
    int toolCallsHeight = 0;
    int imageBlockHeight = 0;
    int textHeight = 0;
};

ItemLayout layoutItem(const QStyleOptionViewItem &option, const QModelIndex &index) {
    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString content = index.data(Qt::DisplayRole).toString();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    const QVector<QImage> images = decodeImages(index);
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);

    const int availableWidth = option.rect.width() - (2 * kEdgeMargin);
    const int maxTextWidth =
        isUser ? qMax(60, (availableWidth * kMaxBubbleWidthFraction / 100) - (2 * kBubblePadding))
               : qMax(60, availableWidth);

    ItemLayout layout;
    layout.toolCallsHeight = toolCallsBlockHeight(toolCalls, option.font, maxTextWidth);
    layout.imageBlockHeight = images.isEmpty() ? 0 : imagesBlockHeight(images) + kImageSpacing;

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
    const int contentWidth =
        qMax(textWidth, qMin(imageBlockWidth, maxTextWidth)) + (2 * kBubblePadding);
    const int contentHeight =
        layout.textHeight + layout.imageBlockHeight + layout.toolCallsHeight + (2 * kBubblePadding);

    const int labelTop = option.rect.top();
    const int contentTop = labelTop + kLabelHeight + 2;

    layout.labelRect = QRect(option.rect.left() + kEdgeMargin, labelTop,
                             option.rect.width() - (2 * kEdgeMargin), kLabelHeight);
    if (isUser) {
        QRect bubbleRect(option.rect.right() - kEdgeMargin - contentWidth, contentTop, contentWidth,
                         contentHeight);
        layout.contentRect = bubbleRect;
    } else {
        // Assistant/tool/error messages use the full chat area: no bubble.
        layout.contentRect =
            QRect(option.rect.left() + kEdgeMargin, contentTop, availableWidth, contentHeight);
    }
    return layout;
}

const QIcon &copyIcon() {
    static const QIcon icon =
        QIcon::fromTheme(QStringLiteral("edit-copy-symbolic"),
                         QIcon(QString::fromUtf8(":/resources/icons/edit-copy-symbolic.svg")));
    return icon;
}

// Rect of the per-message copy button in the label row. Assistant messages
// reserve room at the right for the throughput text so the two never
// collide; user messages put the button on the left ("You" is right).
QRect copyButtonRect(const QStyleOptionViewItem &option, bool isUser, bool hasThroughput) {
    const QRect labelRow(option.rect.left() + kEdgeMargin, option.rect.top(),
                         option.rect.width() - (2 * kEdgeMargin), kLabelHeight);
    QRect rect(0, 0, kCopyButtonSize, kCopyButtonSize);
    rect.moveCenter(labelRow.center());
    if (isUser)
        rect.moveLeft(labelRow.left());
    else if (hasThroughput)
        rect.moveLeft(labelRow.right() - (4 * kCopyButtonSize));
    else
        rect.moveLeft(labelRow.right() - kCopyButtonSize + 1);
    return rect;
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

    // Role label (and throughput) above the content, fully inside the item.
    painter->setPen(QColor(0x50, 0x50, 0x50));
    QFont labelFont = option.font;
    labelFont.setBold(true);
    labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
    painter->setFont(labelFont);
    if (isUser)
        painter->drawText(layout.labelRect, Qt::AlignRight | Qt::AlignVCenter, roleLabel);
    else
        painter->drawText(layout.labelRect, Qt::AlignLeft | Qt::AlignVCenter, roleLabel);
    const double tokensPerSecond = index.data(static_cast<int>(Qt::UserRole) + 4).toDouble();
    const bool hasThroughput = !isUser && tokensPerSecond > 0.0;
    if (hasThroughput) {
        const QString throughput =
            QStringLiteral("%1 tok/s").arg(QString::number(tokensPerSecond, 'f', 1));
        painter->drawText(layout.labelRect, Qt::AlignRight | Qt::AlignVCenter, throughput);
    }

    // Per-message copy button in the label row.
    const QRect copyRect = copyButtonRect(option, isUser, hasThroughput);
    const bool showCopied = m_copiedIndex == index;
    copyIcon().paint(painter, copyRect);
    if (showCopied) {
        QFont hintFont = option.font;
        hintFont.setPointSizeF(qMax(7.0, hintFont.pointSizeF() - 1.0));
        hintFont.setItalic(true);
        painter->setFont(hintFont);
        painter->setPen(QColor(0x2a, 0x82, 0xda));
        const QRect hintRect(copyRect.right() + 4, copyRect.top(),
                             layout.labelRect.right() - copyRect.right() - 4, kLabelHeight);
        painter->drawText(hintRect, Qt::AlignLeft | Qt::AlignVCenter, tr("Copied"));
    }

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

    // Message text.
    painter->setPen(textColor);
    painter->translate(contentRect.left() + kBubblePadding, contentRect.top() + kBubblePadding +
                                                                layout.toolCallsHeight +
                                                                layout.imageBlockHeight);
    doc->drawContents(painter);

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const ItemLayout layout = layoutItem(option, index);
    const int height =
        (layout.contentRect.top() - option.rect.top()) + layout.contentRect.height() + kItemSpacing;
    return {option.rect.width(), height};
}

bool ChatDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                               const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (event->type() != QEvent::MouseButtonRelease)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const auto *mouse = dynamic_cast<const QMouseEvent *>(event);
    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const double tokensPerSecond = index.data(static_cast<int>(Qt::UserRole) + 4).toDouble();
    const bool hasThroughput = !isUser && tokensPerSecond > 0.0;
    if (!mouse || !copyButtonRect(option, isUser, hasThroughput).contains(mouse->pos()))
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
