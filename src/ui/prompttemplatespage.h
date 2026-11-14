#pragma once

#include "core/imagemessage.h"
#include "core/message.h"

#include <QWidget>
#include <memory>

namespace Ui {
class PromptTemplatesPage;
}

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

namespace okapi {

class PromptTemplateRegistry;

// Settings page "Prompt Templates" (Settings > General): create, edit and
// delete prompt templates. A template has a mandatory name and prompt text
// plus optional file attachments; in the composer the trigger shortcut
// (default "/") opens an autocompletion that inserts the selected template.
class PromptTemplatesPage : public QWidget {
    Q_OBJECT

public:
    ~PromptTemplatesPage() override;

    explicit PromptTemplatesPage(PromptTemplateRegistry *registry, QWidget *parent = nullptr);

private slots:
    void onNew();
    void onSave();
    void onDelete();
    void onAddFiles();
    void onRemoveAttachment();

private:
    std::unique_ptr<Ui::PromptTemplatesPage> m_ui;
    void reloadList();
    void loadToForm(const QUuid &id);
    void clearForm();

    PromptTemplateRegistry *m_registry;
    QListWidget *m_list = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QListWidget *m_attachmentsList = nullptr;
    QPushButton *m_newButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_addFilesButton = nullptr;
    QPushButton *m_removeAttachmentButton = nullptr;
    // The template currently loaded in the form (null id = new template).
    QUuid m_editingId;
    QVector<ImageAttachment> m_pendingImages;
    QVector<FileAttachment> m_pendingFiles;
};

} // namespace okapi
