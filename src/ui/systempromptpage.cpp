#include "ui/systempromptpage.h"
#include "ui_systempromptpage.h"

#include "core/systemprompt.h"

#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

namespace okapi {

SystemPromptPage::~SystemPromptPage() = default;

SystemPromptPage::SystemPromptPage(SystemPrompt *systemPrompt, QWidget *parent)
    : QWidget(parent), m_systemPrompt(systemPrompt),
      m_ui(std::make_unique<Ui::SystemPromptPage>()) {
    m_ui->setupUi(this);
    m_editor = m_ui->editor;
    m_preview = m_ui->preview;
    m_saveButton = m_ui->saveButton;
    m_previewButton = m_ui->previewButton;
    m_resetButton = m_ui->resetButton;

    m_editor->setPlainText(m_systemPrompt->text());
    m_preview->setPlaceholderText(tr("Preview of the prompt with substituted fields"));

    connect(m_saveButton, &QPushButton::clicked, this, &SystemPromptPage::onSave);
    connect(m_previewButton, &QPushButton::clicked, this, &SystemPromptPage::onPreview);
    connect(m_resetButton, &QPushButton::clicked, this, &SystemPromptPage::onResetDefault);
}

void SystemPromptPage::onSave() {
    m_systemPrompt->setText(m_editor->toPlainText());
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/okapi.conf"), QSettings::IniFormat);
    m_systemPrompt->save(settings);
    emit changed();
}

void SystemPromptPage::onPreview() {
    m_preview->setPlainText(SystemPrompt::substitute(m_editor->toPlainText()));
}

void SystemPromptPage::onResetDefault() {
    m_editor->setPlainText(SystemPrompt::defaultTemplate());
    onPreview();
}

} // namespace okapi
