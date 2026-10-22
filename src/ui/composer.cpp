#include "ui/composer.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ocapi {

namespace {

constexpr int kMaxAttachmentsPerMessage = 8;
constexpr int kThumbnailSize = 48;

const QStringList &imageSuffixes() {
    static const QStringList allowed = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("jxl"),  QStringLiteral("avif"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("gif")};
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
    return ImagePreprocessor::supportedNameFilter() +
           QStringLiteral(";;PDF documents (*.pdf);;Office documents "
                          "(*.docx *.xlsx *.pptx *.odt *.ods *.odp);;Archives "
                          "(*.zip *.tar *.tar.gz *.tgz *.tar.bz2 *.tar.xz *.7z);;E-mail "
                          "(*.eml *.mbox);;Text files (*)");
}

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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_attachmentStrip = new QWidget(this);
    m_attachmentLayout = new QHBoxLayout(m_attachmentStrip);
    m_attachmentLayout->setContentsMargins(4, 2, 4, 2);
    m_attachmentLayout->setSpacing(4);
    m_attachmentLayout->addStretch(1);
    m_attachmentStrip->hide();
    layout->addWidget(m_attachmentStrip);

    auto *row = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    m_textEdit = new MessageTextEdit(row);
    m_attachButton = new QToolButton(this);
    m_attachButton->setText(QStringLiteral("+"));
    m_attachButton->setToolTip(tr("Attach images, documents or text files"));
    m_attachButton->setAutoRaise(true);
    m_attachButton->setFixedSize(28, 28);
    m_micButton = new QToolButton(this);
    const QIcon micIcon =
        QIcon::fromTheme(QStringLiteral("audio-input-microphone"),
                         QIcon(QString::fromUtf8(":/resources/icons/microphone.svg")));
    if (!micIcon.isNull()) {
        m_micButton->setIcon(micIcon);
        m_micButton->setIconSize(QSize(18, 18));
    } else {
        m_micButton->setText(QString::fromUtf8("\U0001F3A4"));
    }
    m_micButton->setToolTip(tr("Dictate message (speech-to-text)"));
    m_micButton->setAccessibleName(tr("Dictate message"));
    m_micButton->setAutoRaise(true);
    m_micButton->setCheckable(true);
    m_micButton->setFixedSize(28, 28);
    layout->addWidget(row);

    m_textEdit->setAccessibleName(tr("Message input"));
    m_textEdit->setPlaceholderText(
        tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setAcceptDrops(true);

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

bool Composer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_textEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
                return false;
            if (!m_busy) {
                emit sendRequested(composedText(), m_attachments);
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
    m_textEdit->setReadOnly(busy);
    m_attachButton->setEnabled(!busy);
    m_micButton->setEnabled(!busy);
    m_textEdit->setPlaceholderText(
        busy ? tr("Waiting for response…")
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
           m_textBlocks.isEmpty();
}

void Composer::attachFiles() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Attach files"), QString(), nameFilter());
    for (const QString &file : files) {
        if (!isImageFileName(file)) {
            addDocument(file);
            continue;
        }
        if (m_attachments.size() >= kMaxAttachmentsPerMessage)
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
    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Attachment error"), error);
        return;
    }
    addTextBlock(QFileInfo(path).fileName(), text);
}

void Composer::addTextBlock(const QString &fileName, const QString &text) {
    m_textBlocks.push_back(qMakePair(fileName, text.trimmed()));
    refreshAttachmentStrip();
}

QString Composer::composedText() const {
    QStringList parts;
    const QString typed = m_textEdit->toPlainText().trimmed();
    if (!typed.isEmpty())
        parts.push_back(typed);
    for (const auto &block : m_textBlocks)
        parts.push_back(tr("[Attached file: %1]\n%2").arg(block.first, block.second));
    return parts.join(QStringLiteral("\n\n"));
}

void Composer::clearAttachments() {
    m_attachments.clear();
    m_textBlocks.clear();
    refreshAttachmentStrip();
}

void Composer::addAttachment(const ImageAttachment &attachment) {
    if (m_attachments.size() >= kMaxAttachmentsPerMessage) {
        QMessageBox::warning(
            this, tr("Too many images"),
            tr("At most %1 images can be attached to a message.").arg(kMaxAttachmentsPerMessage));
        return;
    }
    m_attachments.push_back(attachment);
    refreshAttachmentStrip();
}

void Composer::refreshAttachmentStrip() {
    while (m_attachmentLayout->count() > 1) {
        auto *item = m_attachmentLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    for (const auto &attachment : m_attachments) {
        auto *thumb = new QLabel(m_attachmentStrip);
        thumb->setFixedSize(kThumbnailSize, kThumbnailSize);
        const QImage image = attachment.decode();
        if (!image.isNull()) {
            thumb->setPixmap(QPixmap::fromImage(image).scaled(
                kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            thumb->setText(QStringLiteral("[image]"));
        }
        const QString tooltip = attachment.resized ? tr("%1 (resized to %2×%3)")
                                                         .arg(attachment.fileName)
                                                         .arg(attachment.width)
                                                         .arg(attachment.height)
                                                   : attachment.fileName;
        thumb->setToolTip(tooltip);
        thumb->setAlignment(Qt::AlignCenter);
        m_attachmentLayout->insertWidget(m_attachmentLayout->count() - 1, thumb);
    }
    for (const auto &block : m_textBlocks) {
        auto *chip = new QLabel(m_attachmentStrip);
        chip->setText(QString::fromUtf8("\U0001F4D1 ") + block.first);
        chip->setToolTip(tr("Text extracted from %1").arg(block.first));
        chip->setAlignment(Qt::AlignCenter);
        chip->setStyleSheet(QStringLiteral("border: 1px solid palette(mid); padding: 2px 6px;"));
        m_attachmentLayout->insertWidget(m_attachmentLayout->count() - 1, chip);
    }
    m_attachmentStrip->setVisible(!m_attachments.isEmpty() || !m_textBlocks.isEmpty());
    emit attachmentsChanged();
}

} // namespace ocapi
