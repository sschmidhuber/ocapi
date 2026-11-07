#include "ui/prompttemplatespage.h"

#include "core/filepreview.h"
#include "core/prompttemplateregistry.h"
#include "core/textextractor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace ocapi {

namespace {

bool isImagePath(const QString &path) {
    static const QStringList suffixes{
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("jxl"),  QStringLiteral("avif"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("gif"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

// Processes one picked file into an image or document attachment, mirroring
// what the composer does on drop/attach.
void appendFileAttachment(const QString &path, QVector<ImageAttachment> &images,
                          QVector<FileAttachment> &files) {
    if (isImagePath(path)) {
        QString error;
        auto image = ImagePreprocessor::processFile(path, &error);
        if (image)
            images.append(*image);
        else
            QMessageBox::warning(nullptr, QObject::tr("Image error"), error);
        return;
    }
    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    if (text.isEmpty()) {
        QMessageBox::warning(nullptr, QObject::tr("Attachment error"), error);
        return;
    }
    FileAttachment file;
    file.fileName = QFileInfo(path).fileName();
    file.mimeType = QStringLiteral("application/octet-stream");
    file.sourcePath = path;
    file.thumbnail = FilePreview::thumbnailForFile(path, 128);
    file.extractedText = text.trimmed();
    files.append(file);
}

} // namespace

PromptTemplatesPage::PromptTemplatesPage(PromptTemplateRegistry *registry, QWidget *parent)
    : QWidget(parent), m_registry(registry) {
    auto *splitter = new QSplitter(this);

    auto *leftWidget = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(leftWidget);
    leftLayout->addWidget(m_list, 1);
    auto *leftButtons = new QHBoxLayout();
    m_newButton = new QPushButton(tr("New"), leftWidget);
    m_deleteButton = new QPushButton(tr("Delete"), leftWidget);
    leftButtons->addWidget(m_newButton);
    leftButtons->addWidget(m_deleteButton);
    leftButtons->addStretch(1);
    leftLayout->addLayout(leftButtons);

    auto *rightWidget = new QWidget(splitter);
    auto *form = new QVBoxLayout(rightWidget);
    form->setContentsMargins(0, 0, 0, 0);
    form->addWidget(new QLabel(tr("Name:"), rightWidget));
    m_nameEdit = new QLineEdit(rightWidget);
    m_nameEdit->setPlaceholderText(tr("Template name (shown in the autocompletion)"));
    form->addWidget(m_nameEdit);
    form->addWidget(new QLabel(tr("Prompt text:"), rightWidget));
    m_textEdit = new QPlainTextEdit(rightWidget);
    m_textEdit->setPlaceholderText(tr("Prompt text inserted into the composer"));
    form->addWidget(m_textEdit, 1);
    form->addWidget(new QLabel(tr("Attachments:"), rightWidget));
    m_attachmentsList = new QListWidget(rightWidget);
    m_attachmentsList->setMaximumHeight(120);
    form->addWidget(m_attachmentsList);
    auto *attachmentButtons = new QHBoxLayout();
    m_addFilesButton = new QPushButton(tr("Add files…"), rightWidget);
    m_removeAttachmentButton = new QPushButton(tr("Remove"), rightWidget);
    attachmentButtons->addWidget(m_addFilesButton);
    attachmentButtons->addWidget(m_removeAttachmentButton);
    attachmentButtons->addStretch(1);
    form->addLayout(attachmentButtons);
    auto *formButtons = new QHBoxLayout();
    m_saveButton = new QPushButton(tr("Save"), rightWidget);
    formButtons->addWidget(m_saveButton);
    formButtons->addStretch(1);
    form->addLayout(formButtons);

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
                if (current)
                    loadToForm(QUuid(current->data(Qt::UserRole).toString()));
            });
    connect(m_newButton, &QPushButton::clicked, this, &PromptTemplatesPage::onNew);
    connect(m_saveButton, &QPushButton::clicked, this, &PromptTemplatesPage::onSave);
    connect(m_deleteButton, &QPushButton::clicked, this, &PromptTemplatesPage::onDelete);
    connect(m_addFilesButton, &QPushButton::clicked, this, &PromptTemplatesPage::onAddFiles);
    connect(m_removeAttachmentButton, &QPushButton::clicked, this,
            &PromptTemplatesPage::onRemoveAttachment);
    connect(m_registry, &PromptTemplateRegistry::changed, this, [this]() { reloadList(); });

    reloadList();
    clearForm();
}

void PromptTemplatesPage::reloadList() {
    const QUuid currentId = m_list->currentItem()
                                ? QUuid(m_list->currentItem()->data(Qt::UserRole).toString())
                                : QUuid();
    m_list->clear();
    for (const auto &t : m_registry->templates()) {
        auto *item = new QListWidgetItem(t.name, m_list);
        item->setData(Qt::UserRole, t.id.toString(QUuid::WithoutBraces));
        item->setToolTip(t.text.left(200));
        if (t.id == currentId)
            m_list->setCurrentItem(item);
    }
}

void PromptTemplatesPage::clearForm() {
    m_editingId = QUuid();
    m_nameEdit->clear();
    m_textEdit->clear();
    m_attachmentsList->clear();
    m_pendingImages.clear();
    m_pendingFiles.clear();
    m_list->setCurrentItem(nullptr);
}

void PromptTemplatesPage::loadToForm(const QUuid &id) {
    const PromptTemplate *t = m_registry->find(id);
    if (!t)
        return;
    m_editingId = id;
    m_nameEdit->setText(t->name);
    m_textEdit->setPlainText(t->text);
    m_pendingImages = t->images;
    m_pendingFiles = t->files;
    m_attachmentsList->clear();
    for (const auto &image : t->images)
        m_attachmentsList->addItem(QStringLiteral("🖼 ") + image.fileName);
    for (const auto &file : t->files)
        m_attachmentsList->addItem(file.fileName);
}

void PromptTemplatesPage::onNew() {
    clearForm();
    m_nameEdit->setFocus();
}

void PromptTemplatesPage::onSave() {
    const QString name = m_nameEdit->text().trimmed();
    const QString text = m_textEdit->toPlainText();
    if (name.isEmpty() || text.trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Incomplete template"),
                             tr("A prompt template needs a name and a prompt text."));
        return;
    }
    PromptTemplate t;
    t.id = m_editingId.isNull() ? QUuid::createUuid() : m_editingId;
    t.name = name;
    t.text = text;
    t.images = m_pendingImages;
    t.files = m_pendingFiles;
    if (m_editingId.isNull() || !m_registry->find(m_editingId))
        m_registry->add(std::move(t));
    else
        m_registry->update(t);
    reloadList();
    // Select the saved template.
    for (int i = 0; i < m_list->count(); ++i) {
        if (QUuid(m_list->item(i)->data(Qt::UserRole).toString()) == t.id) {
            m_list->setCurrentItem(m_list->item(i));
            break;
        }
    }
}

void PromptTemplatesPage::onDelete() {
    auto *item = m_list->currentItem();
    if (!item)
        return;
    const QUuid id(item->data(Qt::UserRole).toString());
    m_registry->remove(id);
    clearForm();
}

void PromptTemplatesPage::onAddFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Attach files to the template"), QString(),
        tr("All supported files (*.jpg *.jpeg *.png *.webp *.jxl *.avif *.tif *.tiff *.gif "
           "*.pdf *.docx *.xlsx *.pptx *.odt *.ods *.odp *.zip *.tar *.tar.gz *.tgz *.tar.bz2 "
           "*.tar.xz *.7z *.eml *.mbox *.txt *.md *.csv *.json *.xml *.yaml *.yml *.log *.c *.cc "
           "*.cpp *.cxx *.h *.hpp *.py *.js *.ts *.java *.rs *.go *.rb *.sh *.sql);;All files "
           "(*)"));
    for (const auto &path : paths) {
        appendFileAttachment(path, m_pendingImages, m_pendingFiles);
        const QString label = QFileInfo(path).fileName();
        m_attachmentsList->addItem(isImagePath(path) ? QStringLiteral("🖼 ") + label : label);
    }
}

void PromptTemplatesPage::onRemoveAttachment() {
    const int row = m_attachmentsList->currentRow();
    if (row < 0)
        return;
    m_attachmentsList->takeItem(row);
    if (row < m_pendingImages.size()) {
        m_pendingImages.remove(row);
    } else {
        const int fileIndex = row - static_cast<int>(m_pendingImages.size());
        if (fileIndex < m_pendingFiles.size())
            m_pendingFiles.remove(fileIndex);
    }
}

} // namespace ocapi
