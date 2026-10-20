#include "ui/chatdelegate.h"
#include "core/message.h"

#include <QApplication>
#include <QPainter>
#include <QTextDocument>

namespace ocapi {

namespace {

constexpr int kBubbleMargin = 12;
constexpr int kBubblePadding = 10;
constexpr int kBubbleRadius = 10;
constexpr int kMaxBubbleWidthFraction = 80; // percent of viewport width

} // namespace

ChatDelegate::ChatDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ChatDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool isUser = index.data(static_cast<int>(Qt::UserRole)).toBool();
    const QString roleLabel = index.data(static_cast<int>(Qt::UserRole) + 1).toString();
    const QString content = index.data(Qt::DisplayRole).toString();
    const bool streaming = index.data(static_cast<int>(Qt::UserRole) + 2).toBool();
    const QString error = index.data(static_cast<int>(Qt::UserRole) + 3).toString();

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
        bubbleColor = option.palette.color(QPalette::Base).darker(option.state & QStyle::State_Enabled ? 104 : 100);
        textColor = option.palette.color(QPalette::Text);
    }

    QTextDocument doc;
    doc.setDefaultFont(option.font);
    const int maxTextWidth = option.rect.width() * kMaxBubbleWidthFraction / 100 - 2 * kBubblePadding;
    doc.setTextWidth(maxTextWidth);
    QString displayText = content;
    if (streaming && !displayText.isEmpty())
        displayText.append(QChar(0x2588)); // block cursor while streaming
    doc.setPlainText(displayText.isEmpty() && streaming ? QStringLiteral("…") : displayText);
    const QSize textSize = doc.size().toSize();

    QRect bubbleRect;
    if (isUser) {
        bubbleRect = QRect(rect.right() - textSize.width() - 2 * kBubblePadding - 30,
                           rect.top(),
                           textSize.width() + 2 * kBubblePadding,
                           textSize.height() + 2 * kBubblePadding + 18);
        bubbleRect.moveRight(rect.right());
    } else {
        bubbleRect = QRect(rect.left() + 30,
                           rect.top(),
                           textSize.width() + 2 * kBubblePadding,
                           textSize.height() + 2 * kBubblePadding + 18);
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
    const QRect labelRect(bubbleRect.left(),
                          bubbleRect.top() - 14,
                          bubbleRect.width(),
                          14);
    painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, roleLabel);

    // Message text inside the bubble.
    painter->setFont(option.font);
    painter->setPen(textColor);
    painter->translate(bubbleRect.left() + kBubblePadding,
                       bubbleRect.top() + kBubblePadding + 8);
    if (!error.isEmpty()) {
        QFont errorFont = option.font;
        errorFont.setItalic(true);
        doc.setDefaultFont(errorFont);
        doc.setPlainText(QStringLiteral("⚠ ") + error);
        doc.setTextWidth(maxTextWidth);
        doc.drawContents(painter);
    } else {
        doc.drawContents(painter);
    }

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QString content = index.data(Qt::DisplayRole).toString();
    QTextDocument doc;
    doc.setDefaultFont(option.font);
    const int maxTextWidth = option.rect.width() > 0
        ? option.rect.width() * kMaxBubbleWidthFraction / 100 - 2 * kBubblePadding
        : 400;
    doc.setTextWidth(maxTextWidth);
    doc.setPlainText(content);
    return QSize(option.rect.width(),
                 doc.size().height() + 2 * kBubblePadding + 18 + 8 + 14);
}

} // namespace ocapi
