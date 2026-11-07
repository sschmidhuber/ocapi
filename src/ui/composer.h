#pragma once

#include "core/imagemessage.h"
#include "core/message.h"
#include "core/textextractor.h"

#include <QKeySequence>
#include <QList>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVector>
#include <QWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QHBoxLayout;
class QMimeData;
class QScrollArea;
class QShortcut;

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
    [[nodiscard]] bool isBusy() const { return m_busy; }
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
    // Height the attachment strip currently adds to the composer (0 when no
    // attachments are shown); the main window grows the composer area by
    // this amount so the text input keeps its height.
    [[nodiscard]] int attachmentStripHeight() const;
    [[nodiscard]] QVector<FileAttachment> documents() const { return m_documents; }
    [[nodiscard]] bool hasAttachments() const {
        return !m_attachments.isEmpty() || !m_documents.isEmpty();
    }

    void attachFiles();
    void clearAttachments();
    // Entry points for drag & drop anywhere in the composer or the chat
    // view (wired up by the main window).
    void addDroppedUrls(const QList<QUrl> &urls);
    void addDroppedImage(const QImage &image);

    // The mic and attach buttons are created here (behavior, tooltips,
    // state handling) but laid out by the parent, so they can share a row
    // with the model selector.
    [[nodiscard]] QToolButton *micButton() const { return m_micButton; }
    [[nodiscard]] QToolButton *attachButton() const { return m_attachButton; }
    // Dictation needs a configured speech-to-text service; hide the mic
    // button entirely while none is available.
    void setDictationAvailable(bool available);
    // Rebinds the dictate shortcut (e.g. after the user changed it in the
    // settings) and updates the mic button tooltip accordingly.
    void setDictateSequence(const QKeySequence &sequence);

signals:
    void sendRequested(const QString &text, const QVector<ocapi::ImageAttachment> &images,
                       const QVector<ocapi::FileAttachment> &files);
    void attachmentsChanged();
    void dictationRequested();

public slots:
    void setDictationActive(bool active);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void addAttachment(const ImageAttachment &attachment);
    void addDocument(const QString &path);
    void removeAttachment(const QUuid &id);
    void removeDocument(const QUuid &id);
    void refreshAttachmentStrip();
    QToolButton *removeButton(QWidget *parent, const QUuid &id, bool isDocument);

    MessageTextEdit *m_textEdit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QToolButton *m_micButton = nullptr;
    QShortcut *m_dictateShortcut = nullptr;
    bool m_dictationActive = false;
    bool m_dictationAvailable = true;
    int m_dictationStart = 0;
    int m_dictatedLength = 0;
    QScrollArea *m_attachmentStrip = nullptr;
    QHBoxLayout *m_attachmentLayout = nullptr;
    QVector<ImageAttachment> m_attachments;
    QVector<FileAttachment> m_documents; // non-image files with thumbnails
    bool m_busy = false;
};

} // namespace ocapi
