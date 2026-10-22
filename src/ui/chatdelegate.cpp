#include "ui/chatdelegate.h"
#include "core/markdown.h"
#include "core/message.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <QJsonDocument>

#include <memory>

namespace ocapi {

namespace {

constexpr int kBubbleMargin = 12;
constexpr int kBubblePadding = 10;
constexpr int kBubbleRadius = 10;
constexpr int kMaxBubbleWidthFraction = 80; // percent of viewport width
constexpr int kImageSize = 160;             // max thumbnail edge in the chat bubble
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

// Builds the text document for a message bubble. Assistant and tool messages
// are rendered as markdown; user messages and errors stay plain text.
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

} // namespace

ChatDelegate::ChatDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void ChatDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const QString roleLabel = index.data(static_cast<int>(Qt::UserRole) + 1).toString();
    const QString content = index.data(Qt::DisplayRole).toString();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    const QVector<QImage> images = decodeImages(index);
    const QVector<ToolCall> toolCalls = decodeToolCalls(index);
    const bool isToolMessage =
        index.data(static_cast<int>(Qt::UserRole) + 1).toString() == QStringLiteral("Tool");
    const QString toolName = index.data(static_cast<int>(Qt::UserRole) + 7).toString();

    const QRect rect = option.rect.adjusted(kBubbleMargin, 4, -kBubbleMargin, -4);

    QColor bubbleColor;
    QColor textColor;
    if (!error.isEmpty()) {
        bubbleColor = QColor(0x5a, 0x1d, 0x1d);
        textColor = QColor(0xff, 0xd7, 0xd7);
    } else if (isUser) {
        bubbleColor = option.palette.color(QPalette::Highlight);
        textColor = option.palette.color(QPalette::HighlightedText);
    } else {
        bubbleColor = option.palette.color(QPalette::Base)
                          .darker((option.state & QStyle::State_Enabled) ? 104 : 100);
        textColor = option.palette.color(QPalette::Text);
    }

    const int maxTextWidth =
        (option.rect.width() * kMaxBubbleWidthFraction / 100) - (2 * kBubblePadding);

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
    // Bubble text uses the bubble text color; markdown inline formats (code
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
    const QSize textSize = doc->size().toSize();

    const int toolCallsHeight = toolCallsBlockHeight(toolCalls, option.font, maxTextWidth);

    const int imageBlockWidth = imagesBlockWidth(images);
    const int imageBlockHeight = images.isEmpty() ? 0 : imagesBlockHeight(images) + kImageSpacing;
    const int contentWidth =
        qMax(textSize.width(), qMin(imageBlockWidth, maxTextWidth)) + (2 * kBubblePadding);
    const int contentHeight =
        textSize.height() + imageBlockHeight + toolCallsHeight + (2 * kBubblePadding) + 18;

    QRect bubbleRect;
    if (isUser) {
        bubbleRect =
            QRect(rect.right() - contentWidth - 30, rect.top(), contentWidth, contentHeight);
        bubbleRect.moveRight(rect.right());
    } else {
        bubbleRect = QRect(rect.left() + 30, rect.top(), contentWidth, contentHeight);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);
    painter->drawRoundedRect(bubbleRect, kBubbleRadius, kBubbleRadius);

    // Role label above the bubble.
    painter->setPen(option.palette.color(QPalette::Text));
    QFont labelFont = option.font;
    labelFont.setBold(true);
    labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
    painter->setFont(labelFont);
    const QRect labelRect(bubbleRect.left(), bubbleRect.top() - 14, bubbleRect.width(), 14);
    painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, roleLabel);
    const double tokensPerSecond = index.data(static_cast<int>(Qt::UserRole) + 4).toDouble();
    if (!isUser && tokensPerSecond > 0.0) {
        const QString throughput =
            QStringLiteral("%1 tok/s").arg(QString::number(tokensPerSecond, 'f', 1));
        painter->drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, throughput);
    }

    // Tool calls requested by the assistant, above the message text.
    int toolY = bubbleRect.top() + kBubblePadding + 8;
    QFont toolFont = option.font;
    toolFont.setItalic(true);
    painter->setFont(toolFont);
    painter->setPen(option.palette.color(QPalette::Text).darker(120));
    for (const auto &call : toolCalls) {
        QTextDocument callDoc;
        callDoc.setDefaultFont(toolFont);
        callDoc.setTextWidth(bubbleRect.width() - (2 * kBubblePadding));
        callDoc.setPlainText(toolCallDisplayText(call));
        const int callHeight = callDoc.size().toSize().height();
        painter->translate(bubbleRect.left() + kBubblePadding, toolY);
        callDoc.drawContents(painter);
        painter->translate(-(bubbleRect.left() + kBubblePadding), -toolY);
        toolY += callHeight + 2;
    }

    // Images above the message text inside the bubble.
    int x = bubbleRect.left() + kBubblePadding;
    const int imageTop = bubbleRect.top() + kBubblePadding + 8 + toolCallsHeight;
    for (const auto &image : images) {
        const int targetWidth = qMin(image.width(), kImageSize);
        const int targetHeight = qMin(image.height(), kImageSize);
        const QRect target(x, imageTop, targetWidth, targetHeight);
        painter->drawImage(target, image);
        x += targetWidth + kImageSpacing;
    }

    // Message text inside the bubble.
    painter->translate(bubbleRect.left() + kBubblePadding,
                       bubbleRect.top() + kBubblePadding + 8 + toolCallsHeight + imageBlockHeight);
    doc->drawContents(painter);

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const QString content = index.data(Qt::DisplayRole).toString();
    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();
    const QVector<QImage> images = decodeImages(index);
    const int maxTextWidth =
        option.rect.width() > 0
            ? (option.rect.width() * kMaxBubbleWidthFraction / 100) - (2 * kBubblePadding)
            : 400;
    int textHeight = 0;
    if (!error.isEmpty()) {
        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setTextWidth(maxTextWidth);
        doc.setPlainText(QStringLiteral("\u26a0 ") + error);
        textHeight = doc.size().toSize().height();
    } else {
        const auto doc = messageDocument(content, !isUser, streaming, option.font, maxTextWidth);
        textHeight = doc->size().toSize().height();
    }
    const int imageBlockHeight = images.isEmpty() ? 0 : imagesBlockHeight(images) + kImageSpacing;
    const int toolCallsHeight =
        toolCallsBlockHeight(decodeToolCalls(index), option.font, maxTextWidth);
    return {option.rect.width(),
            textHeight + imageBlockHeight + toolCallsHeight + (2 * kBubblePadding) + 18 + 8 + 14};
}

} // namespace ocapi
