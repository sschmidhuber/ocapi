#pragma once

#include "core/imagemessage.h"
#include "core/textextractor.h"

#include <QPair>
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
    void documentDropped(const QString &path);

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
    [[nodiscard]] QString composedText() const;
    void clearText();
    // Live dictation display: shows already-spoken words while recording.
    void setDictatedText(const QString &text);
    // Commits the final transcription: replaces the dictated prefix with the
    // final text, appending at the cursor if the user typed meanwhile.
    void finishDictation(const QString &text);
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QVector<ImageAttachment> attachments() const { return m_attachments; }
    [[nodiscard]] bool hasAttachments() const { return !m_attachments.isEmpty(); }

    void attachFiles();
    void clearAttachments();

signals:
    void sendRequested(const QString &text, const QVector<ocapi::ImageAttachment> &images);
    void attachmentsChanged();
    void dictationRequested();

public slots:
    void setDictationActive(bool active);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addAttachment(const ImageAttachment &attachment);
    void addDocument(const QString &path);
    void addTextBlock(const QString &fileName, const QString &text);
    void refreshAttachmentStrip();

    MessageTextEdit *m_textEdit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QToolButton *m_micButton = nullptr;
    bool m_dictationActive = false;
    int m_dictationStart = 0;
    int m_dictatedLength = 0;
    QWidget *m_attachmentStrip = nullptr;
    QHBoxLayout *m_attachmentLayout = nullptr;
    QVector<ImageAttachment> m_attachments;
    QVector<QPair<QString, QString>> m_textBlocks; // (file name, extracted text)
    bool m_busy = false;
};

} // namespace ocapi
