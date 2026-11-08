#pragma once

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

namespace ocapi::debugSanitize {

// Shared redaction/truncation for the env-gated debug logging paths
// (OCAPI_DEBUG_SSE / OCAPI_DEBUG_STT / OCAPI_DEBUG_TTS). Debug output is
// exactly what users paste into bug reports, so credentials must never land
// in it: API keys travel in Authorization headers, x-api-key-style headers
// and key= URL query parameters (Gemini), and api_key JSON fields.

[[nodiscard]] inline QString redact(QString text) {
    static const QRegularExpression headerAuth(
        QStringLiteral(R"re((authorization\s*:\s*(?:bearer\s+)?)[^\r\n,;"]+)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression apiKeyHeader(
        QStringLiteral(R"re(\b((?:x-api-key|xi-api-key)\s*:\s*)[^\r\n,;"]+)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression jsonApiKey(
        QStringLiteral(R"re(("(?:api[_-]?key|apiKey)"\s*:\s*")[^"]*("))re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression urlKeyParam(QStringLiteral(R"re(([?&]key=)[^&\s"]+)re"),
                                                QRegularExpression::CaseInsensitiveOption);
    text.replace(headerAuth, QStringLiteral("\\1<redacted>"));
    text.replace(apiKeyHeader, QStringLiteral("\\1<redacted>"));
    text.replace(jsonApiKey, QStringLiteral("\\1<redacted>\\2"));
    text.replace(urlKeyParam, QStringLiteral("\\1<redacted>"));
    return text;
}

// Clips a logged payload to maxChars and marks the truncation with the
// original length, so long conversations/responses do not flood the log.
[[nodiscard]] inline QString clip(QString text, int maxChars) {
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("…[truncated, %1 chars total]").arg(text.size());
}

[[nodiscard]] inline QByteArray redact(QByteArray data) {
    return redact(QString::fromUtf8(data)).toUtf8();
}

[[nodiscard]] inline QByteArray clip(QByteArray data, int maxBytes) {
    return clip(QString::fromUtf8(data), maxBytes).toUtf8();
}

} // namespace ocapi::debugSanitize
