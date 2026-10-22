#pragma once

#include <QFont>
#include <QString>
#include <QTextDocument>

#include <memory>

namespace ocapi {

// Renders a subset of markdown (headings, emphasis, inline code, fenced code
// blocks, links, lists, blockquotes, horizontal rules) into a QTextDocument
// laid out for the given text width and base font. Assistant and tool messages
// are rendered as markdown; user messages stay plain text.
class MarkdownRenderer {
public:
    // Returns a document laid out to maxTextWidth with the given base font.
    // Documents are cached by (markdown, width, font key); repeated calls with
    // the same arguments (e.g. while a message streams in token by token)
    // return the cached document.
    static std::unique_ptr<QTextDocument> render(const QString &markdown, int maxTextWidth,
                                                 const QFont &font);

    // Clears the document cache. Mainly for tests.
    static void clearCache();

private:
    static void renderInto(QTextDocument &doc, const QString &markdown, const QFont &font);
};

} // namespace ocapi
