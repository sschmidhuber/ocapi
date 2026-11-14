#pragma once

#include <QWidget>
#include <memory>

namespace Ui {
class SystemPromptPage;
}

class QPlainTextEdit;
class QPushButton;

namespace okapi {

class SystemPrompt;

// Settings page for the system prompt: edit the template (with the dynamic
// fields {{date}}, {{time}}, {{language}}, {{os}}), preview it with the
// fields substituted, save it, or reset it to the default.
class SystemPromptPage : public QWidget {
    Q_OBJECT

public:
    ~SystemPromptPage() override;

    explicit SystemPromptPage(SystemPrompt *systemPrompt, QWidget *parent = nullptr);

signals:
    void changed();

private slots:
    void onSave();
    void onPreview();
    void onResetDefault();

private:
    std::unique_ptr<Ui::SystemPromptPage> m_ui;
    SystemPrompt *m_systemPrompt;
    QPlainTextEdit *m_editor = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_previewButton = nullptr;
    QPushButton *m_resetButton = nullptr;
};

} // namespace okapi
