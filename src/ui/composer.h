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
class QKeyEvent;
class QMimeData;
class QScrollArea;
class QShortcut;

class QToolButton;

namespace okapi {

class MessageTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit MessageTextEdit(QWidget *parent = nullptr);

signals:
    void imagePasted(const okapi::ImageAttachment &attachment);
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
    // Loads a message into the composer for editing (text + attachments).
    void setText(const QString &text);
    void setAttachments(const QVector<ImageAttachment> &images,
                        const QVector<FileAttachment> &files);
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
    // Re-applies the theme after a change: text edit palette (so the
    // viewport follows dark mode) and the theme-colored icons.
    void refreshTheme();
    // Rebinds the prompt-template trigger (composer-local, configurable in
    // the settings; default "/"). Detected in eventFilter: text edits accept
    // the shortcut override for printable keys, so a QShortcut never fires.
    void setPromptTriggerSequence(const QKeySequence &sequence);
    // Inserts a prompt template at the cursor: text plus attachments.
    void insertTemplate(const QString &text, const QVector<ImageAttachment> &images,
                        const QVector<FileAttachment> &files);
    // Replaces the typed trigger text ("/re" etc.) with the template.
    void applyPromptTemplate(const QString &text, const QVector<ImageAttachment> &images,
                             const QVector<FileAttachment> &files);
    [[nodiscard]] bool promptMenuActive() const { return m_promptActive; }
    // Global anchor for the prompt-template popup: bottom-left of the line
    // box at the trigger position (IDE-style caret anchoring). Falls back to
    // the current caret when no prompt session is active.
    [[nodiscard]] QPoint promptMenuAnchorGlobal() const;

signals:
    void sendRequested(const QString &text, const QVector<okapi::ImageAttachment> &images,
                       const QVector<okapi::FileAttachment> &files);
    void attachmentsChanged();
    void dictationRequested();
    // The prompt-template trigger was typed (menu opens).
    void promptTemplateRequested();
    // Text after the trigger changed while the menu is open (live filter).
    void promptFilterChanged(const QString &filter);
    // Enter while the menu is open: confirm the selected entry.
    void promptMenuConfirmed();
    // Arrow up/down while the menu is open: move the selection (delta -1/1).
    void promptMenuNavigate(int delta);
    // Escape while the menu is open, or the trigger text was removed.
    void promptMenuClosed();

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
    void onTextChanged();
    void closePromptMenu();
    // Key handling while the prompt-template menu is open (trigger, Enter,
    // Escape); returns true when the key was consumed.
    bool handlePromptMenuKey(QKeyEvent *keyEvent);
    // Typed length of the trigger (1 for a printable single key like "/",
    // 0 for modifier combos like Ctrl+Space which insert nothing).
    [[nodiscard]] int promptTriggerTextLength() const;
    QToolButton *removeButton(QWidget *parent, const QUuid &id, bool isDocument);

    MessageTextEdit *m_textEdit = nullptr;
    QToolButton *m_attachButton = nullptr;
    QToolButton *m_micButton = nullptr;
    QShortcut *m_dictateShortcut = nullptr;
    QKeySequence m_promptTrigger{QStringLiteral("/")};
    bool m_promptActive = false;
    int m_promptTriggerPos = -1;
    bool m_dictationActive = false;
    bool m_dictationAvailable = true;
    int m_dictationStart = 0;
    int m_dictatedLength = 0;
    QScrollArea *m_attachmentStrip = nullptr;
    int m_stripHeight = 0; // fixed strip height incl. scrollbar allowance
    QHBoxLayout *m_attachmentLayout = nullptr;
    QVector<ImageAttachment> m_attachments;
    QVector<FileAttachment> m_documents; // non-image files with thumbnails
    bool m_busy = false;
};

} // namespace okapi
