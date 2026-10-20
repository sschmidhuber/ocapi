#include "ui/composer.h"

#include <QKeyEvent>

namespace ocapi {

Composer::Composer(QWidget *parent) : QPlainTextEdit(parent) {
    setPlaceholderText(tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
    setMaximumBlockCount(0);
    setTabChangesFocus(true);
}

void Composer::setSendEnabled(bool enabled) {
    setReadOnly(!enabled || m_busy);
}

void Composer::setBusy(bool busy) {
    m_busy = busy;
    setReadOnly(busy);
    setPlaceholderText(busy ? tr("Waiting for response…")
                            : tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
}

void Composer::focusInput() {
    setFocus();
}

void Composer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() & Qt::ShiftModifier) {
            QPlainTextEdit::keyPressEvent(event);
            return;
        }
        const QString text = toPlainText().trimmed();
        if (!text.isEmpty() && !m_busy)
            emit sendRequested(text);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

} // namespace ocapi
