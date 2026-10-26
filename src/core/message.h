#pragma once

#include "core/imagemessage.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVector>

namespace ocapi {

enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

// Lifecycle of one tool invocation requested by the assistant. Running
// calls show a progress indicator in the chat; finished calls render their
// result inline when the element is expanded.
enum class ToolCallStatus {
    Running,
    Done,
    Failed,
};

// One tool invocation requested by the assistant (OpenAI-style tool_calls).
struct ToolCall {
    QString id;
    QString name; // namespaced tool name, e.g. "server__tool"
    QJsonObject arguments;
    ToolCallStatus status = ToolCallStatus::Running;
    QString result; // tool output, empty until the call finished

    [[nodiscard]] bool operator==(const ToolCall &other) const {
        return id == other.id && name == other.name && arguments == other.arguments &&
               status == other.status && result == other.result;
    }
};

} // namespace ocapi

Q_DECLARE_METATYPE(ocapi::ToolCall)
Q_DECLARE_METATYPE(QVector<ocapi::ToolCall>)

namespace ocapi {

// A non-image file attached to a chat message (PDF, office document,
// archive, e-mail, plain text, ...). The thumbnail is a small PNG preview
// generated when the file is attached; the full document is embedded into
// the message text as extracted text.
struct FileAttachment {
    QUuid id = QUuid::createUuid();
    QString fileName;
    QString mimeType;
    QString sourcePath;
    QByteArray thumbnail;  // encoded PNG preview of the file's first page
    QString extractedText; // text sent to the model (set when the file is attached)

    [[nodiscard]] QImage decodeThumbnail() const { return QImage::fromData(thumbnail); }
};

} // namespace ocapi

Q_DECLARE_METATYPE(ocapi::FileAttachment)
Q_DECLARE_METATYPE(QVector<ocapi::FileAttachment>)

namespace ocapi {

struct Message {
    QUuid id = QUuid::createUuid();
    Role role = Role::User;
    QString content;
    QDateTime timestamp = QDateTime::currentDateTime();
    bool streaming = false;
    // Reasoning ("thinking") tokens streamed by reasoning-capable models
    // before the visible answer. Shown in a collapsed "Thinking" element.
    QString reasoning;
    // True while reasoning tokens are still arriving; the collapsed element
    // shows a progress indicator until the answer starts.
    bool reasoningStreaming = false;
    QString error;
    double tokensPerSecond = -1.0;   // output tokens/s; negative when unknown
    QVector<ImageAttachment> images; // images attached to this message
    QVector<FileAttachment> files;   // non-image files attached to this message
    QVector<ToolCall> toolCalls;     // tool invocations requested by the assistant
    // For Role::Tool: which tool call this message answers.
    QString toolCallId;
    QString toolName;

    [[nodiscard]] bool hasImages() const { return !images.isEmpty(); }
    [[nodiscard]] bool hasFiles() const { return !files.isEmpty(); }
    // Text sent to the model: the typed message plus the extracted content
    // of every attached file. The chat UI shows only the typed content; the
    // file contents travel with the attachments instead of the message text.
    [[nodiscard]] QString textForModel() const {
        QString text = content;
        for (const auto &file : files) {
            if (file.extractedText.isEmpty())
                continue;
            text += QStringLiteral("\n\n[Attached file: %1]\n%2")
                        .arg(file.fileName, file.extractedText);
        }
        return text;
    }
    [[nodiscard]] bool hasToolCalls() const { return !toolCalls.isEmpty(); }
    [[nodiscard]] bool isEmpty() const {
        return content.isEmpty() && images.isEmpty() && files.isEmpty() && toolCalls.isEmpty();
    }

    [[nodiscard]] QString roleName() const {
        switch (role) {
        case Role::System:
            return QStringLiteral("system");
        case Role::User:
            return QStringLiteral("user");
        case Role::Assistant:
            return QStringLiteral("assistant");
        case Role::Tool:
            return QStringLiteral("tool");
        }
        return QStringLiteral("unknown");
    }
};

} // namespace ocapi
