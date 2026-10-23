#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace ocapi {

// Categorizes attached non-image files so the chat can render a matching
// thumbnail for every supported file type.
enum class FileCategory {
    Pdf,
    Word,         // .docx / .odt
    Spreadsheet,  // .xlsx / .ods
    Presentation, // .pptx / .odp
    Archive,
    Email,
    Text,
    Generic,
};

// Renders a small PNG preview (thumbnail) of a supported file. PDFs render
// their first page via Poppler; office documents, archives and e-mails get
// a generic icon tile; text files get a page tile with a text glyph.
// Returns an empty QImage when no preview can be produced.
class FilePreview {
public:
    // Category of a file, derived from its extension (lower-cased suffix).
    [[nodiscard]] static FileCategory categoryForFile(const QString &path);

    // One-letter badge drawn on generic tiles, empty for page-like tiles.
    [[nodiscard]] static QString badgeForCategory(FileCategory category);

    // Generates the PNG-encoded thumbnail for a file, scaled to fit
    // maxEdge. Returns an empty QByteArray when no preview is available.
    [[nodiscard]] static QByteArray thumbnailForFile(const QString &path, int maxEdge);

    [[nodiscard]] static int maxThumbnailEdge() { return 96; }
};

} // namespace ocapi
