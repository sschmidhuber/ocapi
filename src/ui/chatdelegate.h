#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
#include <QTimer>

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

signals:
    // Fired when the "Copied" hint appears or disappears; the view uses it
    // to schedule a repaint.
    void copiedFeedbackChanged();

private:
    // Briefly shows a "Copied" hint on the message that was copied.
    void showCopiedFeedback(const QModelIndex &index);

    QPersistentModelIndex m_copiedIndex;
    QTimer m_copiedTimer;
};

} // namespace ocapi
