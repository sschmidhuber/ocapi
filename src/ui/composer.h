#pragma once

#include "core/imagemessage.h"

#include <QPlainTextEdit>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QMimeData;

class QToolButton;

namespace ocapi {

class MessageTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit MessageTextEdit(QWidget *parent = nullptr);

signals:
    void imagePasted(const ocapi::ImageAttachment &attachment);
    void imagePasteFailed(const QString &error);

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
};

class Composer : public QWidget {
    Q_OBJECT

public:
    explicit Composer(QWidget *parent = nullptr);

    void setSendEnabled(bool enabled);
    void setBusy(bool busy);
    void focusInput();

    [[nodiscard]] QString text() const;
    void clearText();
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QVector<ImageAttachment> attachments() const { return m_attachments; }
    [[nodiscard]] bool hasAttachments() const { return !m_attachments.isEmpty(); }

    void attachImages();
    void clearAttachments();

signals:
    void sendRequested(const QString &text, const QVector<ocapi::ImageAttachment> &images);
    void attachmentsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addAttachment(const ImageAttachment &attachment);
    void refreshAttachmentStrip();

    MessageTextEdit *m_textEdit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QWidget *m_attachmentStrip = nullptr;
    QHBoxLayout *m_attachmentLayout = nullptr;
    QVector<ImageAttachment> m_attachments;
    bool m_busy = false;
};

} // namespace ocapi
