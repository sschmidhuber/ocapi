#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QTimer>

class QHelpEvent;
class QAbstractItemView;

namespace ocapi {

class ChatDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ChatDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

    // True when \p pos (in item coordinates, with \p itemRect the item's
    // rect in viewport coordinates) lies on a clickable attachment. Used by
    // the view to switch the cursor to a pointing hand.
    [[nodiscard]] static bool attachmentAt(const QModelIndex &index, const QRect &itemRect,
                                           const QPoint &pos);

signals:
    // Fired when the "Copied" hint appears or disappears; the view uses it
    // to schedule a repaint.
    void copiedFeedbackChanged();
    // Fired when opening a clicked attachment failed; the view shows the
    // error to the user.
    void attachmentOpenFailed(const QString &error);
    // Fired when the hovered attachment changes; the view repaints the item.
    void hoverChanged();

private:
    // Briefly shows a "Copied" hint on the message that was copied.
    void showCopiedFeedback(const QModelIndex &index);
    // Repaints the item when the hovered attachment changes.
    void setHoveredAttachment(const QModelIndex &index, int attachmentIndex);
    // Handles a mouse release: attachment open, tool-call toggle or copy.
    bool handleMouseRelease(QEvent *event, QAbstractItemModel *model,
                            const QStyleOptionViewItem &option, const QModelIndex &index);

    QPersistentModelIndex m_copiedIndex;
    QTimer m_copiedTimer;
    // Index of the attachment (image first, then files) currently hovered,
    // or -1. Combined with the item index to repaint only when it changes.
    QPersistentModelIndex m_hoveredIndex;
    int m_hoveredAttachment = -1;
};

} // namespace ocapi
