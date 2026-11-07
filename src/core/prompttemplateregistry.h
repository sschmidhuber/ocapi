#pragma once

#include "core/imagemessage.h"
#include "core/message.h"

#include <QObject>
#include <QSettings>
#include <QUuid>
#include <QVector>

namespace ocapi {

// One prompt template: a name shown in the composer's autocompletion, a
// mandatory prompt text and optional file attachments. Inserted into the
// composer (text at the cursor + attachments) when selected.
struct PromptTemplate {
    QUuid id = QUuid::createUuid();
    QString name;
    QString text;
    QVector<ImageAttachment> images;
    QVector<FileAttachment> files;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static PromptTemplate fromJson(const QJsonObject &object);
};

// Registry of the user's prompt templates (Settings > General > Prompt
// Templates), persisted as JSON in the application settings under
// "promptTemplates". Mirrors the TtsRegistry pattern: load() remembers the
// file so changes can save() immediately; every change emits changed().
class PromptTemplateRegistry : public QObject {
    Q_OBJECT

public:
    explicit PromptTemplateRegistry(QObject *parent = nullptr);

    [[nodiscard]] const QVector<PromptTemplate> &templates() const { return m_templates; }
    [[nodiscard]] const PromptTemplate *find(const QUuid &id) const;
    // Adds a new template (id must be fresh); returns the stored template.
    PromptTemplate &add(PromptTemplate tpl);
    // Replaces the template with the same id.
    void update(const PromptTemplate &tpl);
    void remove(const QUuid &id);

    void load(QSettings &settings);
    void save(QSettings &settings) const;
    void save() const;

signals:
    void changed();

private:
    QVector<PromptTemplate> m_templates;
    QString m_persistPath;
    QSettings::Format m_persistFormat = QSettings::IniFormat;
};

} // namespace ocapi
