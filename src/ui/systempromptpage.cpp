#include "ui/systempromptpage.h"

#include "core/systemprompt.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace okapi {

SystemPromptPage::SystemPromptPage(SystemPrompt *systemPrompt, QWidget *parent)
    : QWidget(parent), m_systemPrompt(systemPrompt) {
    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(tr("Dynamic fields: {{date}}, {{time}}, {{language}}, {{os}}"), this);
    layout->addWidget(hint);

    m_editor = new QPlainTextEdit(this);
    m_editor->setPlainText(m_systemPrompt->text());
    layout->addWidget(m_editor, 1);

    auto *buttonRow = new QHBoxLayout();
    m_saveButton = new QPushButton(tr("Save"), this);
    m_previewButton = new QPushButton(tr("Preview"), this);
    m_resetButton = new QPushButton(tr("Reset to default"), this);
    buttonRow->addWidget(m_saveButton);
    buttonRow->addWidget(m_previewButton);
    buttonRow->addWidget(m_resetButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setPlaceholderText(tr("Preview of the prompt with substituted fields"));
    layout->addWidget(m_preview, 1);

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
