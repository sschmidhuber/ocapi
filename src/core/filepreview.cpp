#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QBuffer>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

namespace ocapi {

namespace {

// Tile colors per category, matching the free-desktop palette conventions.
QColor tileColor(FileCategory category) {
    switch (category) {
    case FileCategory::Pdf:
        return {0xb7, 0x1c, 0x1c};
    case FileCategory::Word:
        return {0x1a, 0x5f, 0xb4};
    case FileCategory::Spreadsheet:
        return {0x1e, 0x7d, 0x32};
    case FileCategory::Presentation:
        return {0xd3, 0x6b, 0x1c};
    case FileCategory::Archive:
        return {0x6d, 0x4c, 0x41};
    case FileCategory::Email:
        return {0x5e, 0x35, 0xb1};
    case FileCategory::Text:
        return {0x45, 0x5a, 0x64};
    case FileCategory::Generic:
        return {0x75, 0x75, 0x75};
    }
    return {0x75, 0x75, 0x75};
}

// Draws a white page with a folded corner plus a colored badge with the
// category letter, used for file types without a real page preview.
QImage iconTile(FileCategory category, int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal margin = size * 0.18;
    const qreal pageWidth = size - (2 * margin);
    const qreal pageHeight = pageWidth * 1.3;
    const qreal fold = pageWidth * 0.22;
    const qreal x = (size - pageWidth) / 2.0;
    const qreal y = (size - pageHeight) / 2.0;

    QPainterPath page;
    page.moveTo(x, y);
    page.lineTo(x + pageWidth - fold, y);
    page.lineTo(x + pageWidth, y + fold);
    page.lineTo(x + pageWidth, y + pageHeight);
    page.lineTo(x, y + pageHeight);
    page.closeSubpath();
    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.setBrush(Qt::white);
    painter.drawPath(page);
    QPainterPath foldPath;
    foldPath.moveTo(x + pageWidth - fold, y);
    foldPath.lineTo(x + pageWidth, y + fold);
    foldPath.lineTo(x + pageWidth - fold, y + fold);
    foldPath.closeSubpath();
    painter.setBrush(QColor(0xe0, 0xe0, 0xe0));
    painter.drawPath(foldPath);

    const QString badge = FilePreview::badgeForCategory(category);
    if (!badge.isEmpty()) {
        const qreal badgeSize = pageWidth * 0.62;
        const QRectF badgeRect(x + ((pageWidth - badgeSize) / 2.0),
                               y + ((pageHeight - badgeSize) / 2.0), badgeSize, badgeSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tileColor(category));
        painter.drawRoundedRect(badgeRect, badgeSize * 0.18, badgeSize * 0.18);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(static_cast<int>(badgeSize * 0.52));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, badge);
    }
    painter.end();
    return tile;
}

// Draws a text page tile: white page, folded corner and dark text lines.
QImage textTile(int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal margin = size * 0.18;
    const qreal pageWidth = size - (2 * margin);
    const qreal pageHeight = pageWidth * 1.3;
    const qreal fold = pageWidth * 0.22;
    const qreal x = (size - pageWidth) / 2.0;
    const qreal y = (size - pageHeight) / 2.0;

    QPainterPath page;
    page.moveTo(x, y);
    page.lineTo(x + pageWidth - fold, y);
    page.lineTo(x + pageWidth, y + fold);
    page.lineTo(x + pageWidth, y + pageHeight);
    page.lineTo(x, y + pageHeight);
    page.closeSubpath();
    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.setBrush(Qt::white);
    painter.drawPath(page);
    QPainterPath foldPath;
    foldPath.moveTo(x + pageWidth - fold, y);
    foldPath.lineTo(x + pageWidth, y + fold);
    foldPath.lineTo(x + pageWidth - fold, y + fold);
    foldPath.closeSubpath();
    painter.setBrush(QColor(0xe0, 0xe0, 0xe0));
    painter.drawPath(foldPath);

    painter.setPen(QPen(tileColor(FileCategory::Text), qMax(1.0, size * 0.035)));
    const qreal lineLeft = x + (pageWidth * 0.18);
    const qreal lineRight = x + (pageWidth * 0.82);
    const qreal lineSpacing = pageHeight * 0.13;
    qreal lineY = y + (pageHeight * 0.28);
    while (lineY < y + (pageHeight * 0.82)) {
        painter.drawLine(QPointF(lineLeft, lineY), QPointF(lineRight, lineY));
        lineY += lineSpacing;
    }
    painter.end();
    return tile;
}

QByteArray encodePng(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

} // namespace

FileCategory FilePreview::categoryForFile(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return FileCategory::Pdf;
    if (suffix == QLatin1String("docx") || suffix == QLatin1String("odt"))
        return FileCategory::Word;
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("ods"))
        return FileCategory::Spreadsheet;
    if (suffix == QLatin1String("pptx") || suffix == QLatin1String("odp"))
        return FileCategory::Presentation;
    if (suffix == QLatin1String("zip") || suffix == QLatin1String("tar") ||
        suffix == QLatin1String("gz") || suffix == QLatin1String("tgz") ||
        suffix == QLatin1String("bz2") || suffix == QLatin1String("xz") ||
        suffix == QLatin1String("7z"))
        return FileCategory::Archive;
    if (suffix == QLatin1String("eml") || suffix == QLatin1String("mbox"))
        return FileCategory::Email;
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("md") ||
        suffix == QLatin1String("markdown") || suffix == QLatin1String("csv") ||
        suffix == QLatin1String("json") || suffix == QLatin1String("xml") ||
        suffix == QLatin1String("yaml") || suffix == QLatin1String("yml") ||
        suffix == QLatin1String("log") || suffix == QLatin1String("html") ||
        suffix == QLatin1String("htm") || suffix == QLatin1String("c") ||
        suffix == QLatin1String("cc") || suffix == QLatin1String("cpp") ||
        suffix == QLatin1String("cxx") || suffix == QLatin1String("h") ||
        suffix == QLatin1String("hpp") || suffix == QLatin1String("py") ||
        suffix == QLatin1String("js") || suffix == QLatin1String("ts") ||
        suffix == QLatin1String("java") || suffix == QLatin1String("rs") ||
        suffix == QLatin1String("go") || suffix == QLatin1String("rb") ||
        suffix == QLatin1String("sh") || suffix == QLatin1String("sql"))
        return FileCategory::Text;
    return FileCategory::Generic;
}

QString FilePreview::badgeForCategory(FileCategory category) {
    switch (category) {
    case FileCategory::Pdf:
        return QStringLiteral("PDF");
    case FileCategory::Word:
        return QStringLiteral("W");
    case FileCategory::Spreadsheet:
        return QStringLiteral("X");
    case FileCategory::Presentation:
        return QStringLiteral("P");
    case FileCategory::Archive:
        return QStringLiteral("ZIP");
    case FileCategory::Email:
        return QStringLiteral("@");
    case FileCategory::Text:
    case FileCategory::Generic:
        return {};
    }
    return {};
}

QByteArray FilePreview::thumbnailForFile(const QString &path, int maxEdge) {
    if (maxEdge <= 0)
        return {};
    const FileCategory category = categoryForFile(path);

    QImage preview;
    if (category == FileCategory::Pdf)
        preview = TextExtractor::renderFirstPdfPage(path, maxEdge);
    if (preview.isNull()) {
        if (category == FileCategory::Text)
            preview = textTile(maxEdge);
        else
            preview = iconTile(category, maxEdge);
    }
    if (preview.isNull())
        return {};
    if (qMax(preview.width(), preview.height()) > maxEdge)
        preview = preview.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return encodePng(preview);
}

} // namespace ocapi
