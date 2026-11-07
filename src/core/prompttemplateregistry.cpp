#include "core/prompttemplateregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ocapi {

namespace {

constexpr auto kTemplatesKey = "promptTemplates";

QJsonObject imageToJson(const ImageAttachment &image) {
    return {
        {QStringLiteral("fileName"), image.fileName},
        {QStringLiteral("mimeType"), image.mimeType},
        {QStringLiteral("sourcePath"), image.sourcePath},
        {QStringLiteral("width"), image.width},
        {QStringLiteral("height"), image.height},
        {QStringLiteral("resized"), image.resized},
        {QStringLiteral("data"), QString::fromLatin1(image.data.toBase64())},
    };
}

ImageAttachment imageFromJson(const QJsonObject &object) {
    ImageAttachment image;
    image.fileName = object.value(QStringLiteral("fileName")).toString();
    image.mimeType = object.value(QStringLiteral("mimeType")).toString();
    image.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    image.width = object.value(QStringLiteral("width")).toInt();
    image.height = object.value(QStringLiteral("height")).toInt();
    image.resized = object.value(QStringLiteral("resized")).toBool();
    image.data = QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
    return image;
}

QJsonObject fileToJson(const FileAttachment &file) {
    return {
        {QStringLiteral("fileName"), file.fileName},
        {QStringLiteral("mimeType"), file.mimeType},
        {QStringLiteral("sourcePath"), file.sourcePath},
        {QStringLiteral("thumbnail"), QString::fromLatin1(file.thumbnail.toBase64())},
        {QStringLiteral("extractedText"), file.extractedText},
    };
}

FileAttachment fileFromJson(const QJsonObject &object) {
    FileAttachment file;
    file.fileName = object.value(QStringLiteral("fileName")).toString();
    file.mimeType = object.value(QStringLiteral("mimeType")).toString();
    file.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
    file.thumbnail =
        QByteArray::fromBase64(object.value(QStringLiteral("thumbnail")).toString().toLatin1());
    file.extractedText = object.value(QStringLiteral("extractedText")).toString();
    return file;
}

} // namespace

QJsonObject PromptTemplate::toJson() const {
    QJsonArray images;
    for (const auto &image : this->images)
        images.append(imageToJson(image));
    QJsonArray files;
    for (const auto &file : this->files)
        files.append(fileToJson(file));
    return {
        {QStringLiteral("id"), id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), name},
        {QStringLiteral("text"), text},
        {QStringLiteral("images"), images},
        {QStringLiteral("files"), files},
    };
}

PromptTemplate PromptTemplate::fromJson(const QJsonObject &object) {
    PromptTemplate t;
    t.id = QUuid(object.value(QStringLiteral("id")).toString());
    t.name = object.value(QStringLiteral("name")).toString();
    t.text = object.value(QStringLiteral("text")).toString();
    const QJsonArray images = object.value(QStringLiteral("images")).toArray();
    for (const auto &value : images)
        t.images.append(imageFromJson(value.toObject()));
    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    for (const auto &value : files)
        t.files.append(fileFromJson(value.toObject()));
    return t;
}

PromptTemplateRegistry::PromptTemplateRegistry(QObject *parent) : QObject(parent) {}

const PromptTemplate *PromptTemplateRegistry::find(const QUuid &id) const {
    for (const auto &t : m_templates) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

PromptTemplate &PromptTemplateRegistry::add(PromptTemplate template) {
    m_templates.append(std::move(template));
    save();
    emit changed();
    return m_templates.last();
}

void PromptTemplateRegistry::update(const PromptTemplate &template) {
    for (auto &t : m_templates) {
        if (t.id == template.id) {
            t = template;
            save();
            emit changed();
            return;
        }
    }
}

void PromptTemplateRegistry::remove(const QUuid &id) {
    for (int i = 0; i < m_templates.size(); ++i) {
        if (m_templates.at(i).id == id) {
            m_templates.remove(i);
            save();
            emit changed();
            return;
        }
    }
}

void PromptTemplateRegistry::load(QSettings &settings) {
    m_templates.clear();
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(QString::fromLatin1(kTemplatesKey)).toString().toUtf8());
    const QJsonArray array = document.array();
    for (const auto &value : array) {
        PromptTemplate t = PromptTemplate::fromJson(value.toObject());
        if (!t.id.isNull())
            m_templates.append(t);
    }
    m_persistPath = settings.fileName();
    m_persistFormat = settings.format();
    emit changed();
}

void PromptTemplateRegistry::save(QSettings &settings) const {
    QJsonArray array;
    for (const auto &t : m_templates)
        array.append(t.toJson());
    settings.setValue(QString::fromLatin1(kTemplatesKey),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void PromptTemplateRegistry::save() const {
    if (m_persistPath.isEmpty())
        return;
    QSettings settings(m_persistPath, m_persistFormat);
    save(settings);
}

} // namespace ocapi
