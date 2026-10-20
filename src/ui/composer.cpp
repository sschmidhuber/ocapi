#include "ui/composer.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ocapi {

namespace {

constexpr int kMaxAttachmentsPerMessage = 8;
constexpr int kThumbnailSize = 48;

bool isImageFile(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
    static const QStringList allowed = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("jxl"),  QStringLiteral("avif"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("gif")};
    return allowed.contains(suffix);
}

} // namespace

MessageTextEdit::MessageTextEdit(QWidget *parent) : QPlainTextEdit(parent) {}

bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const {
    if (source->hasImage())
        return true;
    if (source->hasUrls()) {
        for (const QUrl &url : source->urls())
            if (isImageFile(url))
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
            if (!isImageFile(url))
                continue;
            QString error;
            auto attachment = ImagePreprocessor::processFile(url.toLocalFile(), &error);
            if (attachment) {
                emit imagePasted(*attachment);
                handled = true;
            } else {
                emit imagePasteFailed(error);
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
    m_attachButton = new QToolButton(row);
    m_attachButton->setText(QStringLiteral("+"));
    m_attachButton->setToolTip(tr("Attach images (JPEG, PNG, WebP, JPEG XL, AVIF, TIFF, GIF)"));
    m_attachButton->setAutoRaise(true);
    m_attachButton->setFixedSize(28, 28);
    rowLayout->addWidget(m_textEdit, 1);
    rowLayout->addWidget(m_attachButton);
    layout->addWidget(row);

    m_textEdit->setPlaceholderText(
        tr("Type a message…  (Enter to send, Shift+Enter for a new line)"));
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setAcceptDrops(true);

    m_textEdit->installEventFilter(this);
    connect(m_attachButton, &QToolButton::clicked, this, &Composer::attachImages);
    connect(m_textEdit, &MessageTextEdit::imagePasted, this,
            [this](const ImageAttachment &attachment) { addAttachment(attachment); });
    connect(m_textEdit, &MessageTextEdit::imagePasteFailed, this,
            [this](const QString &error) { QMessageBox::warning(this, tr("Image error"), error); });
}

bool Composer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_textEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = dynamic_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
                return false;
            if (!m_busy) {
                emit sendRequested(m_textEdit->toPlainText().trimmed(), m_attachments);
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

bool Composer::isEmpty() const {
    return m_textEdit->toPlainText().trimmed().isEmpty() && m_attachments.isEmpty();
}

void Composer::attachImages() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Attach images"), QString(), ImagePreprocessor::supportedNameFilter());
    for (const QString &file : files) {
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

void Composer::clearAttachments() {
    m_attachments.clear();
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
    m_attachmentStrip->setVisible(!m_attachments.isEmpty());
    emit attachmentsChanged();
}

} // namespace ocapi
