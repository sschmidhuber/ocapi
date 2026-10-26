#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace ocapi {

// Extracts the visible text of a streamed message's "content" field.
// Content is usually a plain string, but servers that stream reasoning
// (e.g. Z.ai's GLM models via OpenAI-compatible APIs) send it as an array
// of typed parts, where answer text arrives as {"type":"text"} parts and
// reasoning as {"type":"thinking"} parts. Reading it with toString()
// alone silently dropped the first token of a reply when the server
// switched from parts to plain strings mid-stream.
inline QString ocapiContentText(const QJsonValue &content) {
    if (content.isString())
        return content.toString();
    if (!content.isArray())
        return {};
    QString text;
    const QJsonArray parts = content.toArray();
    for (const auto &partValue : parts) {
        const QJsonObject part = partValue.toObject();
        if (part.value(QStringLiteral("type")).toString() != QStringLiteral("text"))
            continue;
        // {"type":"text","text":...} with either a string or an array of
        // {"type":"text","text":...} segments.
        const QJsonValue textValue = part.value(QStringLiteral("text"));
        if (textValue.isString()) {
            text += textValue.toString();
        } else if (textValue.isArray()) {
            for (const auto &segment : textValue.toArray())
                text += segment.toObject().value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

} // namespace ocapi
