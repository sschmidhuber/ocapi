#include "ui/composer.h"

#include "core/attachmentopener.h"
#include "core/filepreview.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollArea>
#include <QShortcut>
#include <QStyle>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace okapi {

namespace {

constexpr int kMaxAttachmentsPerMessage = 50;
constexpr int kDocumentThumbSize = 128;
constexpr int kDocumentChipPadding = 6;
// Attachment chip: thumbnail on top, file name below it; the thumbnail keeps
// its fixed size, the chip grows vertically to fit the name line.
constexpr int kAttachmentChipWidth = kDocumentThumbSize + (2 * kDocumentChipPadding);
constexpr int kAttachmentChipHeight = kDocumentThumbSize + 22;
// The strip lives in a fixed-height scroll area, so its height never changes
// with the number of attachments (the text input must not shrink when a
// second file is added). The fixed height additionally reserves room for the
// horizontal scrollbar, so chips stay fully visible (thumbnail + name) when
// it appears.
constexpr int kAttachmentStripBaseHeight = kAttachmentChipHeight + 4;
constexpr int kScrollBarAllowance = 18; // fallback when the style reports 0

const QStringList &imageSuffixes() {
    static const QStringList allowed = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("jxl"),  QStringLiteral("avif"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("gif"),
    };
    return allowed;
}

bool isImageFile(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    return imageSuffixes().contains(QFileInfo(url.toLocalFile()).suffix().toLower());
}

bool isImageFileName(const QString &path) {
    return imageSuffixes().contains(QFileInfo(path).suffix().toLower());
}

bool isDocumentFile(const QUrl &url) {
    return url.isLocalFile();
}

QString nameFilter() {
    // "All supported files" is the first entry, so the dialog defaults to
    // it; the specific categories remain selectable as alternatives.
    return QStringLiteral("All supported files (*.jpg *.jpeg *.png *.webp *.jxl *.avif "
                          "*.tif *.tiff *.gif *.pdf *.docx *.xlsx *.pptx *.odt *.ods *.odp "
                          "*.zip *.tar *.tar.gz *.tgz *.tar.bz2 *.tar.xz *.7z *.eml *.mbox "
                          "*.txt *.md *.csv *.json *.xml *.yaml *.yml *.log *.c *.cc *.cpp "
                          "*.cxx *.h *.hpp *.py *.js *.ts *.java *.rs *.go *.rb *.sh *.sql);;") +
           ImagePreprocessor::supportedNameFilter() +
           QStringLiteral(";;PDF documents (*.pdf);;Office documents "
                          "(*.docx *.xlsx *.pptx *.odt *.ods *.odp);;Archives "
                          "(*.zip *.tar *.tar.gz *.tgz *.tar.bz2 *.tar.xz *.7z);;E-mail "
                          "(*.eml *.mbox);;All files (*)");
}

// An attachment chip in the composer strip: clicking it (outside the
// remove button) opens the file with the system's default application.
class AttachmentChip : public QWidget {
    Q_OBJECT

public:
    AttachmentChip(const QString &sourcePath, const QString &toolTip, QWidget *parent = nullptr)
        : QWidget(parent), m_sourcePath(sourcePath) {
        setToolTip(toolTip);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(QFileInfo(sourcePath).fileName());
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            QString error;
            if (!AttachmentOpener::open(m_sourcePath, &error)) {
                // Non-modal so a missing default application never blocks
                // the composer.
                auto *box = new QMessageBox(QMessageBox::Warning, tr("Could not open attachment"),
                                            error, QMessageBox::Ok, this);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->setModal(false);
                box->show();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QString m_sourcePath;
};

} // namespace

MessageTextEdit::MessageTextEdit(QWidget *parent) : QPlainTextEdit(parent) {}

bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const {
    if (source->hasImage())
        return true;
    if (source->hasUrls()) {
        for (const QUrl &url : source->urls())
            if (isImageFile(url) || isDocumentFile(url))
                return true;
    }
    return QPlainTextEdit::canInsertFromMimeData(source);
}

void MessageTextEdit::insertFromMimeData(const QMimeData *source) {
    if (source->hasImage()) {
        const auto image = qvariant_cast<QImage>(source->imageData());
        QString error;
        auto attachment = ImagePreprocessor::processImage(image, QStringLiteral("image/png"),
                                                          tr("pasted-image.png"), &error);
        if (attachment)
            emit imagePasted(*attachment);
        else
            emit imagePasteFailed(error);
        return;
    }
    if (source->hasUrls()) {
        bool handled = false;
        for (const QUrl &url : source->urls()) {
            if (isImageFile(url)) {
                QString error;
                auto attachment = ImagePreprocessor::processFile(url.toLocalFile(), &error);
                if (attachment) {
                    emit imagePasted(*attachment);
                    handled = true;
                } else {
                    emit imagePasteFailed(error);
                }
            } else if (isDocumentFile(url)) {
                emit documentDropped(url.toLocalFile());
                handled = true;
            }
        }
        if (handled)
            return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

Composer::Composer(QWidget *parent) : QWidget(parent) {
    // Drops are accepted on the whole composer (text edit, attachment strip,
    // buttons), not only on the text edit; a highlight marks the drop target.
    setAcceptDrops(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Fixed-height horizontal scroll area: adding more attachments never
    // changes the strip height, so the text input keeps its size and the
    // chat area shrinks exactly once when the first file is added.
    m_attachmentStrip = new QScrollArea(this);
    m_attachmentStrip->setWidgetResizable(true);
    m_attachmentStrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_attachmentStrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_attachmentStrip->setFrameShape(QFrame::NoFrame);
    // Reserve the style's real scrollbar extent so chips (thumbnail + name)
    // stay fully visible when the horizontal scrollbar appears.
    const int sbExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    m_stripHeight = kAttachmentStripBaseHeight + (sbExtent > 0 ? sbExtent : kScrollBarAllowance);
    m_attachmentStrip->setFixedHeight(m_stripHeight);
    auto *stripContent = new QWidget(m_attachmentStrip);
    m_attachmentLayout = new QHBoxLayout(stripContent);
    m_attachmentLayout->setContentsMargins(4, 2, 4, 2);
    m_attachmentLayout->setSpacing(4);
    m_attachmentLayout->addStretch(1);
    m_attachmentStrip->setWidget(stripContent);
    m_attachmentStrip->hide();
    layout->addWidget(m_attachmentStrip);

    auto *row = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    m_textEdit = new MessageTextEdit(row);
    m_attachButton = new QToolButton(this);
    const QIcon attachIcon(QString::fromUtf8(":/resources/icons/folder-new-symbolic.svg"));
    if (!attachIcon.isNull()) {
        m_attachButton->setIcon(attachIcon);
        m_attachButton->setIconSize(QSize(18, 18));
    } else {
        m_attachButton->setText(QStringLiteral("+"));
    }
    m_attachButton->setToolTip(tr("Attach images, documents or text files"));
    m_attachButton->setAutoRaise(true);
    m_attachButton->setFixedSize(28, 28);
    m_micButton = new QToolButton(this);
    const QIcon micIcon(QString::fromUtf8(":/resources/icons/microphone.svg"));
    if (!micIcon.isNull()) {
        m_micButton->setIcon(micIcon);
        m_micButton->setIconSize(QSize(18, 18));
    } else {
        m_micButton->setText(QString::fromUtf8("\U0001F3A4"));
    }
    const QKeySequence dictateSequence(QStringLiteral("Ctrl+."));
    m_micButton->setToolTip(tr("Dictate message (speech-to-text) (%1)")
                                .arg(dictateSequence.toString(QKeySequence::NativeText)));
    m_micButton->setAccessibleName(tr("Dictate message"));
    m_micButton->setAutoRaise(true);
    m_micButton->setCheckable(true);
    m_micButton->setFixedSize(28, 28);
    // The mic/attach buttons are laid out by MainWindow next to the model
    // picker; the text edit fills the composer row.
    rowLayout->addWidget(m_textEdit, 1);
    layout->addWidget(row);

    m_textEdit->setAccessibleName(tr("Message input"));
    m_textEdit->setPlaceholderText(
        tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setAcceptDrops(true);
    // Full frame around the input, including the top side.
    m_textEdit->setStyleSheet(QStringLiteral("QPlainTextEdit { border: 1px solid palette(mid); }"));

    m_textEdit->installEventFilter(this);
    connect(m_attachButton, &QToolButton::clicked, this, &Composer::attachFiles);
    connect(m_textEdit, &MessageTextEdit::imagePasted, this,
            [this](const ImageAttachment &attachment) { addAttachment(attachment); });
    connect(m_textEdit, &MessageTextEdit::imagePasteFailed, this,
            [this](const QString &error) { QMessageBox::warning(this, tr("Image error"), error); });
    connect(m_textEdit, &MessageTextEdit::documentDropped, this,
            [this](const QString &path) { addDocument(path); });
    connect(m_micButton, &QToolButton::toggled, this, [this](bool checked) {
        if (checked == m_dictationActive)
            return;
        emit dictationRequested();
    });
    m_dictateShortcut = new QShortcut(dictateSequence, this);
    m_dictateShortcut->setContext(Qt::WindowShortcut);
    connect(m_dictateShortcut, &QShortcut::activated, this, [this]() {
        if (m_dictationAvailable && !m_busy)
            emit dictationRequested();
    });
    // Live filter while the prompt-template menu is open.
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this]() { onTextChanged(); });
}

void Composer::setPromptTriggerSequence(const QKeySequence &sequence) {
    m_promptTrigger = sequence.isEmpty() ? QKeySequence(QStringLiteral("/")) : sequence;
    if (m_promptActive)
        closePromptMenu();
}

int Composer::promptTriggerTextLength() const {
    // A printable single key (e.g. "/") is inserted as one character;
    // modifier combos (Ctrl+Space) insert nothing.
    if (m_promptTrigger.count() == 1 &&
        (m_promptTrigger[0].toCombined() & ~static_cast<quint32>(Qt::KeyboardModifierMask)) != 0)
        return 1;
    return 0;
}

void Composer::onTextChanged() {
    if (!m_promptActive)
        return;
    const QString text = m_textEdit->toPlainText();
    const int cursor = m_textEdit->textCursor().position();
    const int triggerLen = promptTriggerTextLength();
    const QString triggerText = m_promptTrigger.toString(QKeySequence::PortableText);
    const bool valid = cursor >= m_promptTriggerPos &&
                       (triggerLen == 0 || text.mid(m_promptTriggerPos, triggerLen) == triggerText);
    if (!valid) {
        closePromptMenu();
        return;
    }
    const int filterStart = m_promptTriggerPos + triggerLen;
    emit promptFilterChanged(text.mid(filterStart, cursor - filterStart));
}

void Composer::closePromptMenu() {
    if (!m_promptActive)
        return;
    m_promptActive = false;
    m_promptTriggerPos = -1;
    emit promptMenuClosed();
}

QPoint Composer::promptMenuAnchorGlobal() const {
    QTextCursor cursor = m_textEdit->textCursor();
    if (m_promptActive && m_promptTriggerPos >= 0)
        cursor.setPosition(m_promptTriggerPos);
    const QRect rect = m_textEdit->cursorRect(cursor);
    return m_textEdit->viewport()->mapToGlobal(rect.bottomLeft());
}

void Composer::applyPromptTemplate(const QString &text, const QVector<ImageAttachment> &images,
                                   const QVector<FileAttachment> &files) {
    // Remove the typed trigger + filter text ("/re"), then insert the template.
    QTextCursor cursor = m_textEdit->textCursor();
    const int end = cursor.position();
    cursor.setPosition(m_promptTriggerPos >= 0 ? m_promptTriggerPos : end);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    closePromptMenu();
    insertTemplate(text, images, files);
}

void Composer::insertTemplate(const QString &text, const QVector<ImageAttachment> &images,
                              const QVector<FileAttachment> &files) {
    if (!text.isEmpty())
        m_textEdit->insertPlainText(text);
    for (const auto &image : images) {
        if (m_attachments.size() + m_documents.size() >= kMaxAttachmentsPerMessage)
            break;
        m_attachments.append(image);
    }
    for (const auto &file : files) {
        if (m_attachments.size() + m_documents.size() >= kMaxAttachmentsPerMessage)
            break;
        m_documents.append(file);
    }
    refreshAttachmentStrip();
    m_textEdit->setFocus();
}

void Composer::setDictateSequence(const QKeySequence &sequence) {
    if (!m_dictateShortcut)
        return;
    m_dictateShortcut->setKey(sequence);
    m_micButton->setToolTip(tr("Dictate message (speech-to-text) (%1)")
                                .arg(sequence.toString(QKeySequence::NativeText)));
}

void Composer::setDictationAvailable(bool available) {
    m_dictationAvailable = available;
    m_micButton->setVisible(available);
    if (!available) {
        m_dictationActive = false;
        m_micButton->setChecked(false);
    }
}

void Composer::setDictationActive(bool active) {
    m_dictationActive = active;
    m_micButton->setChecked(active);
    if (active) {
        m_dictationStart = static_cast<int>(m_textEdit->toPlainText().size());
        m_dictatedLength = 0;
        m_textEdit->setPlaceholderText(tr("Listening… speak now, press the microphone button "
                                          "again to stop"));
    } else {
        setBusy(m_busy);
    }
}

// Key handling for the prompt-template menu. The trigger itself is detected
// here because text edits accept the shortcut override for printable keys, so
// a QShortcut would never fire; single printable triggers are matched via the
// produced text (layout-independent: '/' is Shift+7 on German layouts), key
// combos via key|modifiers. Returns true when the key was consumed.
bool Composer::handlePromptMenuKey(QKeyEvent *keyEvent) {
    bool isTrigger = false;
    if (!m_promptTrigger.isEmpty()) {
        if (m_promptTrigger.count() == 1)
            isTrigger = keyEvent->text() == m_promptTrigger.toString();
        else
            isTrigger = QKeySequence(keyEvent->key() | static_cast<int>(keyEvent->modifiers())) ==
                        m_promptTrigger;
    }
    if (!m_promptActive && isTrigger) {
        m_promptActive = true;
        m_promptTriggerPos = m_textEdit->textCursor().position();
        emit promptTemplateRequested();
        return false; // let the trigger character be inserted normally
    }
    if (!m_promptActive)
        return false;
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
            emit promptMenuConfirmed();
            return true;
        }
        return false;
    }
    if (keyEvent->key() == Qt::Key_Escape) {
        closePromptMenu();
        return true;
    }
    // Arrow keys move the menu selection instead of the text caret.
    if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
        emit promptMenuNavigate(keyEvent->key() == Qt::Key_Down ? 1 : -1);
        return true;
    }
    return false;
}

bool Composer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_textEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (handlePromptMenuKey(keyEvent))
            return true;
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
                return false;
            if (!m_busy) {
                emit sendRequested(composedText(), m_attachments, m_documents);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Composer::setSendEnabled(bool enabled) {
    m_textEdit->setReadOnly(!enabled || m_busy);
}

void Composer::setBusy(bool busy) {
    m_busy = busy;
    // The input stays editable while a response streams, so the user can
    // already type a follow-up question; it is queued by the main window and
    // sent when the current turn finishes.
    m_textEdit->setPlaceholderText(
        busy ? tr("Type a follow-up… (sent when the response finishes)")
             : tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
}

void Composer::focusInput() {
    m_textEdit->setFocus();
}

QString Composer::text() const {
    return m_textEdit->toPlainText();
}

void Composer::clearText() {
    m_textEdit->clear();
}

void Composer::setDictatedText(const QString &text) {
    // Preserve the user-typed prefix; replace any previously dictated text.
    const QString typed = m_textEdit->toPlainText().left(m_dictationStart);
    m_dictatedLength = static_cast<int>(text.size());
    m_textEdit->setPlainText(typed + text);
    m_textEdit->moveCursor(QTextCursor::End);
}

void Composer::finishDictation(const QString &text) {
    const QString typed = m_textEdit->toPlainText().left(m_dictationStart);
    QString rest = m_textEdit->toPlainText().mid(m_dictationStart);
    rest = rest.left(std::max<int>(0, static_cast<int>(rest.size()) - m_dictatedLength));
    m_textEdit->setPlainText(typed + text + rest);
    m_textEdit->moveCursor(QTextCursor::End);
    m_dictatedLength = 0;
}

bool Composer::isEmpty() const {
    return m_textEdit->toPlainText().trimmed().isEmpty() && m_attachments.isEmpty() &&
           m_documents.isEmpty();
}

void Composer::attachFiles() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Attach files"), QString(), nameFilter());
    for (const QString &file : files) {
        if (!isImageFileName(file)) {
            addDocument(file);
            continue;
        }
        if (m_attachments.size() + m_documents.size() >= kMaxAttachmentsPerMessage)
            break;
        QString error;
        auto attachment = ImagePreprocessor::processFile(file, &error);
        if (attachment)
            addAttachment(*attachment);
        else
            QMessageBox::warning(this, tr("Image error"), error);
    }
}

void Composer::addDocument(const QString &path) {
    if (m_attachments.size() + m_documents.size() >= kMaxAttachmentsPerMessage) {
        QMessageBox::warning(
            this, tr("Too many attachments"),
            tr("At most %1 files can be attached to a message.").arg(kMaxAttachmentsPerMessage));
        return;
    }
    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Attachment error"), error);
        return;
    }

    FileAttachment file;
    file.fileName = QFileInfo(path).fileName();
    file.mimeType = QStringLiteral("application/octet-stream");
    file.sourcePath = path;
    file.thumbnail = FilePreview::thumbnailForFile(path, kDocumentThumbSize);
    file.extractedText = text.trimmed();
    m_documents.push_back(file);
    refreshAttachmentStrip();
}

void Composer::setText(const QString &text) {
    m_textEdit->setPlainText(text);
}

void Composer::setAttachments(const QVector<ImageAttachment> &images,
                              const QVector<FileAttachment> &files) {
    m_attachments = images;
    m_documents = files;
    refreshAttachmentStrip();
}

QString Composer::composedText() const {
    return m_textEdit->toPlainText();
}

void Composer::clearAttachments() {
    m_attachments.clear();
    m_documents.clear();
    refreshAttachmentStrip();
}

void Composer::addDroppedImage(const QImage &image) {
    QString error;
    auto attachment = ImagePreprocessor::processImage(image, QStringLiteral("image/png"),
                                                      tr("pasted-image.png"), &error);
    if (attachment)
        addAttachment(*attachment);
    else
        QMessageBox::warning(this, tr("Image error"), error);
}

void Composer::addDroppedUrls(const QList<QUrl> &urls) {
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        if (isImageFile(url)) {
            QString error;
            auto attachment = ImagePreprocessor::processFile(url.toLocalFile(), &error);
            if (attachment)
                addAttachment(*attachment);
            else
                QMessageBox::warning(this, tr("Image error"), error);
        } else {
            addDocument(url.toLocalFile());
        }
    }
}

void Composer::dragEnterEvent(QDragEnterEvent *event) {
    const QMimeData *mime = event->mimeData();
    const bool droppable =
        mime->hasImage() ||
        std::ranges::any_of(mime->urls(), [](const QUrl &url) { return url.isLocalFile(); });
    if (droppable) {
        event->acceptProposedAction();
        setStyleSheet(QStringLiteral("Composer { border: 2px dashed palette(highlight); "
                                     "border-radius: 6px; }"));
    } else {
        event->ignore();
    }
}

void Composer::dragLeaveEvent(QDragLeaveEvent *event) {
    setStyleSheet(QString());
    QWidget::dragLeaveEvent(event);
}

void Composer::dropEvent(QDropEvent *event) {
    setStyleSheet(QString());
    const QMimeData *mime = event->mimeData();
    if (mime->hasImage()) {
        addDroppedImage(qvariant_cast<QImage>(mime->imageData()));
        event->acceptProposedAction();
        return;
    }
    if (mime->hasUrls()) {
        addDroppedUrls(mime->urls());
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void Composer::addAttachment(const ImageAttachment &attachment) {
    if (m_attachments.size() + m_documents.size() >= kMaxAttachmentsPerMessage) {
        QMessageBox::warning(
            this, tr("Too many attachments"),
            tr("At most %1 files can be attached to a message.").arg(kMaxAttachmentsPerMessage));
        return;
    }
    m_attachments.push_back(attachment);
    refreshAttachmentStrip();
}

QToolButton *Composer::removeButton(QWidget *parent, const QUuid &id, bool isDocument) {
    auto *button = new QToolButton(parent);
    // Always use the bundled breeze icon: theme lookups resolve to whatever
    // the installed icon theme provides, which may differ between (older or
    // newer) KDE installations and other desktops.
    const QIcon icon(QString::fromUtf8(":/resources/icons/edit-delete-symbolic.svg"));
    if (!icon.isNull())
        button->setIcon(icon);
    else
        button->setText(QString::fromUtf8("\u2715"));
    button->setToolTip(tr("Remove attachment"));
    button->setAutoRaise(true);
    button->setFixedSize(20, 20);
    if (isDocument)
        connect(button, &QToolButton::clicked, this, [this, id] { removeDocument(id); });
    else
        connect(button, &QToolButton::clicked, this, [this, id] { removeAttachment(id); });
    return button;
}

void Composer::removeAttachment(const QUuid &id) {
    for (qsizetype i = m_attachments.size() - 1; i >= 0; --i) {
        if (m_attachments.at(i).id == id)
            m_attachments.removeAt(i);
    }
    refreshAttachmentStrip();
}

void Composer::removeDocument(const QUuid &id) {
    for (qsizetype i = m_documents.size() - 1; i >= 0; --i) {
        if (m_documents.at(i).id == id)
            m_documents.removeAt(i);
    }
    refreshAttachmentStrip();
}

void Composer::refreshAttachmentStrip() {
    while (m_attachmentLayout->count() > 1) {
        auto *item = m_attachmentLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    // One chip per attachment: thumbnail on top (fixed size), file name
    // below it, remove button in the name row.
    auto addChip = [this](const QString &sourcePath, const QString &toolTip,
                          const QImage &thumbImage, const QString &placeholder,
                          const QString &fileName, const QUuid &id, bool isDocument) {
        auto *chip = new AttachmentChip(sourcePath, toolTip, m_attachmentStrip->widget());
        chip->setFixedSize(kAttachmentChipWidth, kAttachmentChipHeight);
        auto *chipLayout = new QVBoxLayout(chip);
        chipLayout->setContentsMargins(kDocumentChipPadding, 2, kDocumentChipPadding, 2);
        chipLayout->setSpacing(2);

        auto *thumb = new QLabel(chip);
        thumb->setFixedSize(kDocumentThumbSize, kDocumentThumbSize);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (!thumbImage.isNull()) {
            thumb->setPixmap(QPixmap::fromImage(thumbImage)
                                 .scaled(kDocumentThumbSize, kDocumentThumbSize,
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            thumb->setText(placeholder);
        }
        chipLayout->addWidget(thumb, 0, Qt::AlignHCenter);

        auto *bottomRow = new QHBoxLayout();
        bottomRow->setContentsMargins(0, 0, 0, 0);
        bottomRow->setSpacing(2);
        auto *label = new QLabel(chip);
        label->setToolTip(fileName);
        label->setText(
            label->fontMetrics().elidedText(fileName, Qt::ElideMiddle, kDocumentThumbSize));
        label->setMaximumWidth(kDocumentThumbSize);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        bottomRow->addWidget(label, 1);
        bottomRow->addWidget(removeButton(chip, id, isDocument));
        chipLayout->addLayout(bottomRow);

        m_attachmentLayout->insertWidget(m_attachmentLayout->count() - 1, chip);
    };
    for (const auto &attachment : m_attachments) {
        const QString tooltip = attachment.resized
                                    ? tr("%1 (resized to %2\u00d7%3)\nClick to open")
                                          .arg(attachment.fileName)
                                          .arg(attachment.width)
                                          .arg(attachment.height)
                                    : tr("%1\nClick to open").arg(attachment.fileName);
        addChip(attachment.sourcePath, tooltip, attachment.decode(), QStringLiteral("[image]"),
                attachment.fileName, attachment.id, false);
    }
    for (const auto &file : m_documents) {
        addChip(file.sourcePath, tr("%1\nClick to open").arg(file.fileName), file.decodeThumbnail(),
                QStringLiteral("?"), file.fileName, file.id, true);
    }
    m_attachmentStrip->setVisible(!m_attachments.isEmpty() || !m_documents.isEmpty());
    emit attachmentsChanged();
}

int Composer::attachmentStripHeight() const {
    if (m_attachments.isEmpty() && m_documents.isEmpty())
        return 0;
    return m_stripHeight;
}

} // namespace okapi

#include "composer.moc"
