#include "core/textextractor.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <cstring>
#include <memory>

#if OCAPI_HAVE_POPPLER
#include "poppler/cpp/poppler-document.h"
#include "poppler/cpp/poppler-global.h"
#include "poppler/cpp/poppler-page-renderer.h"
#include "poppler/cpp/poppler-page.h"
#endif

#if OCAPI_HAVE_TESSERACT
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#endif

#if OCAPI_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace ocapi {

#if OCAPI_HAVE_POPPLER
namespace {

QString toQString(const poppler::ustring &text) {
    const poppler::byte_array utf8 = text.to_utf8();
    return QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()),
                             static_cast<qsizetype>(utf8.size()));
}

QImage renderPage(const poppler::page *page) {
    poppler::page_renderer renderer;
    renderer.set_render_hint(poppler::page_renderer::antialiasing);
    renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
    constexpr double kDpi = 200.0;
    const poppler::image rendered = renderer.render_page(page, kDpi, kDpi);
    if (!rendered.is_valid() || rendered.format() != poppler::image::format_argb32)
        return {};
    const int width = rendered.width();
    const int height = rendered.height();
    QImage image(width, height, QImage::Format_ARGB32);
    if (width <= 0 || height <= 0)
        return {};
    const int bytesPerRow = rendered.bytes_per_row();
    const char *src = rendered.const_data();
    for (int y = 0; y < height; ++y)
        memcpy(image.scanLine(y), src + y * bytesPerRow, static_cast<size_t>(width) * 4);
    return image;
}

// Renders a page at a resolution so the longest page edge is maxEdge pixels.
QImage renderPageScaled(const poppler::page *page, int maxEdge) {
    const poppler::rectf pageRect = page->page_rect();
    const double pageWidth = pageRect.width();
    const double pageHeight = pageRect.height();
    if (pageWidth <= 0.0 || pageHeight <= 0.0 || maxEdge <= 0)
        return {};
    const qreal scale = static_cast<qreal>(maxEdge) / qMax(pageWidth, pageHeight);
    const poppler::image rendered =
        poppler::page_renderer().render_page(page, scale * 72.0, scale * 72.0);
    if (!rendered.is_valid() || rendered.format() != poppler::image::format_argb32)
        return {};
    const int width = rendered.width();
    const int height = rendered.height();
    if (width <= 0 || height <= 0)
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    const int bytesPerRow = rendered.bytes_per_row();
    const char *src = rendered.const_data();
    for (int y = 0; y < height; ++y)
        memcpy(image.scanLine(y), src + y * bytesPerRow, static_cast<size_t>(width) * 4);
    return image;
}

} // namespace
#endif

bool TextExtractor::hasPoppler() {
#if OCAPI_HAVE_POPPLER
    return true;
#else
    return false;
#endif
}

bool TextExtractor::hasTesseract() {
#if OCAPI_HAVE_TESSERACT
    return true;
#else
    return false;
#endif
}

bool TextExtractor::hasLibArchive() {
#if OCAPI_HAVE_LIBARCHIVE
    return true;
#else
    return false;
#endif
}

QStringList TextExtractor::supportedMimeTypes() {
    QStringList types;
    if (hasPoppler())
        types.push_back(QStringLiteral("application/pdf"));
    if (hasTesseract()) {
        types.push_back(QStringLiteral("image/jpeg"));
        types.push_back(QStringLiteral("image/png"));
        types.push_back(QStringLiteral("image/webp"));
        types.push_back(QStringLiteral("image/tiff"));
    }
    if (hasLibArchive()) {
        types.push_back(QStringLiteral("application/vnd.openxmlformats-officedocument."
                                       "wordprocessingml.document"));
        types.push_back(QStringLiteral("application/vnd.openxmlformats-officedocument."
                                       "spreadsheetml.sheet"));
        types.push_back(QStringLiteral("application/vnd.openxmlformats-officedocument."
                                       "presentationml.presentation"));
        types.push_back(QStringLiteral("application/vnd.oasis.opendocument.text"));
        types.push_back(QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"));
        types.push_back(QStringLiteral("application/vnd.oasis.opendocument.presentation"));
    }
    return types;
}

QString TextExtractor::extractFromPdf(const QString &path, QString *error) {
#if OCAPI_HAVE_POPPLER
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QObject::tr("File does not exist: %1").arg(path);
        return {};
    }
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(path.toStdString()));
    if (!doc || doc->is_locked()) {
        if (error)
            *error = QObject::tr("Could not open PDF file: %1").arg(QFileInfo(path).fileName());
        return {};
    }
    const int pageCount = doc->pages();
    if (pageCount <= 0) {
        if (error)
            *error =
                QObject::tr("The PDF file contains no pages: %1").arg(QFileInfo(path).fileName());
        return {};
    }
    QString extracted;
    for (int i = 0; i < pageCount; ++i) {
        std::unique_ptr<poppler::page> page(doc->create_page(i));
        if (!page)
            continue;
        QString pageText = toQString(page->text()).trimmed();
        if (pageText.isEmpty()) {
            // Scanned page without embedded text: render and OCR it.
#if OCAPI_HAVE_TESSERACT
            const QImage rendered = renderPage(page.get());
            if (!rendered.isNull()) {
                QString ocrError;
                pageText = extractFromImage(rendered, &ocrError).trimmed();
            }
#else
            Q_UNUSED(page);
#endif
        }
        if (!pageText.isEmpty())
            extracted += pageText + QStringLiteral("\n\n");
    }
    if (extracted.isEmpty() && error)
        *error = QObject::tr("No text found in PDF file: %1").arg(QFileInfo(path).fileName());
    return extracted;
#else
    Q_UNUSED(path);
    if (error)
        *error = QObject::tr("PDF support is not available in this build (Poppler is missing).");
    return {};
#endif
}

QImage TextExtractor::renderFirstPdfPage(const QString &path, int maxEdge) {
#if OCAPI_HAVE_POPPLER
    if (!QFileInfo::exists(path) || maxEdge <= 0)
        return {};
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(path.toStdString()));
    if (!doc || doc->is_locked() || doc->pages() <= 0)
        return {};
    std::unique_ptr<poppler::page> page(doc->create_page(0));
    if (!page)
        return {};
    return renderPageScaled(page.get(), maxEdge);
#else
    Q_UNUSED(path);
    Q_UNUSED(maxEdge);
    return {};
#endif
}

QString TextExtractor::extractFromImage(const QImage &image, QString *error) {
#if OCAPI_HAVE_TESSERACT
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("Cannot extract text from an empty image.");
        return {};
    }
    tesseract::TessBaseAPI api;
    if (api.Init(nullptr, "eng") != 0) {
        if (error)
            *error = QObject::tr("Could not initialize OCR (Tesseract).");
        return {};
    }
    QImage rgb = image;
    if (rgb.format() != QImage::Format_RGB32 && rgb.format() != QImage::Format_ARGB32)
        rgb = rgb.convertToFormat(QImage::Format_RGB32);
    api.SetImage(rgb.constBits(), rgb.width(), rgb.height(), 4, rgb.bytesPerLine());
    char *text = api.GetUTF8Text();
    const QString result = text ? QString::fromUtf8(text) : QString();
    delete[] text;
    api.End();
    return result;
#else
    Q_UNUSED(image);
    if (error)
        *error =
            QObject::tr("Text recognition is not available in this build (Tesseract is missing).");
    return {};
#endif
}

QString TextExtractor::extractFromImageFile(const QString &path, QString *error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("Could not decode image: %1 (%2)")
                         .arg(QFileInfo(path).fileName(), reader.errorString());
        return {};
    }
    return extractFromImage(image, error);
}

QString TextExtractor::extractFromTextFile(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Could not read file: %1").arg(path);
        return {};
    }
    const QByteArray raw = file.readAll();
    file.close();
    QString text = QString::fromUtf8(raw);
    if (text.isEmpty() && !raw.isEmpty())
        text = QString::fromLatin1(raw);
    if (text.size() > maxTextFileChars()) {
        text.truncate(maxTextFileChars());
        text += QStringLiteral("\n[... file truncated ...]");
    }
    return text;
}

bool TextExtractor::isTextFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    static constexpr qint64 kProbeSize = 8192;
    const QByteArray probe = file.read(kProbeSize);
    file.close();
    if (probe.isEmpty())
        return false;
    if (probe.contains('\0'))
        return false;
    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString decoded = utf8Decoder.decode(probe);
    const bool validUtf8 = !utf8Decoder.hasError();
    Q_UNUSED(decoded);
    int printable = 0;
    const auto total = static_cast<double>(probe.size());
    for (const char c : probe) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc == '\t' || uc == '\n' || uc == '\r' || (uc >= 0x20 && uc != 0x7F))
            ++printable;
    }
    static constexpr double kMinPrintableRatio = 0.85;
    return validUtf8 && static_cast<double>(printable) / total >= kMinPrintableRatio;
}

#if OCAPI_HAVE_LIBARCHIVE
namespace {

// Text in office payload XML: OOXML stores it in dedicated run elements
// (<w:t> Word, <t> Excel, <a:t> PowerPoint). OpenDocument instead nests the
// character data directly inside paragraph elements (<text:p>, <text:h>) and
// inline spans (<text:span>), so for ODF the paragraph end marks a text
// boundary. Both variants are handled by the same token scan.
QString extractXmlText(QByteArray xml, bool odfMode) {
    QString out;
    QXmlStreamReader xmlReader(xml);
    QString currentText;
    bool inText = false;
    while (!xmlReader.atEnd()) {
        const auto token = xmlReader.readNext();
        const QStringView name = xmlReader.name();
        if (token == QXmlStreamReader::StartElement) {
            if (name == QLatin1String("t") || name == QLatin1String("w:t") ||
                name == QLatin1String("a:t")) {
                inText = true;
                currentText.clear();
            } else if (odfMode && name == QLatin1String("p")) {
                // ODF paragraph: <text:p>text...</text:p>
                inText = true;
                currentText.clear();
            } else if (odfMode && name == QLatin1String("h")) {
                inText = true;
                currentText.clear();
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (inText)
                currentText += xmlReader.text();
        } else if (token == QXmlStreamReader::EndElement) {
            const bool textRun = name == QLatin1String("t") || name == QLatin1String("w:t") ||
                                 name == QLatin1String("a:t");
            const bool odfBlock =
                odfMode && (name == QLatin1String("p") || name == QLatin1String("h"));
            if (inText && (textRun || odfBlock)) {
                inText = false;
                if (!currentText.isEmpty()) {
                    if (!out.isEmpty())
                        out += QString(QLatin1Char('\n'));
                    out += currentText;
                }
            }
        }
    }
    return out;
}

} // namespace
#endif

// HTML -> readable text: strip script/style blocks, then all tags, keep
// block-level structure as line breaks and collapse runs of whitespace.
QString TextExtractor::htmlToText(const QByteArray &html) {
    static const QRegularExpression kScriptStyle(
        QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QString text = QString::fromUtf8(html);
    text.remove(kScriptStyle);
    static const QRegularExpression kComments(QStringLiteral("<!--.*?-->"),
                                              QRegularExpression::DotMatchesEverythingOption);
    text.remove(kComments);
    QString out;
    out.reserve(text.size());
    static const QRegularExpression kTag(QStringLiteral("<[^>]*>"));
    qsizetype pos = 0;
    while (pos < text.size()) {
        const auto match = kTag.match(text, pos);
        if (!match.hasMatch()) {
            out += text.mid(pos);
            break;
        }
        out += text.mid(pos, match.capturedStart() - pos);
        const QString tag = match.captured(0).toLower();
        const bool isBlock =
            tag.startsWith("<p") || tag.startsWith("<div") || tag.startsWith("<br") ||
            tag.startsWith("<li") || tag.startsWith("<tr") || tag.startsWith("<h") ||
            tag.startsWith("<table") || tag.startsWith("<section") || tag.startsWith("<article") ||
            tag.startsWith("<header") || tag.startsWith("<footer") || tag.startsWith("<blockquote");
        if (isBlock)
            out += QLatin1Char('\n');
        pos = match.capturedEnd();
    }
    static const QRegularExpression kBlankRuns(QStringLiteral("\n{3,}"));
    out.replace(kBlankRuns, QStringLiteral("\n\n"));
    static const QRegularExpression kSpacedNewlines(QStringLiteral("[ \\t]*\n[ \\t]*"));
    out.replace(kSpacedNewlines, QStringLiteral("\n"));
    return out.trimmed();
}

// One RFC 5322 message: headers of interest + text/plain body (or the text
// stripped from a text/html alternative part).
static QString emailMessageToText(const QByteArray &raw) {
    // Header/body separator: CRLF (RFC 5322) or bare LF (common in mbox).
    qsizetype headerEnd = raw.indexOf("\r\n\r\n");
    qsizetype sepLen = 4;
    if (headerEnd < 0) {
        headerEnd = raw.indexOf("\n\n");
        sepLen = 2;
    }
    if (headerEnd < 0)
        return {};
    const QByteArray headerBlock = raw.left(headerEnd);
    const QByteArray body = raw.mid(headerEnd + sepLen);
    const auto headerValue = [&headerBlock](const char *name) {
        const QByteArray prefix = QByteArray(name) + ":";
        const qsizetype start = headerBlock.indexOf(prefix);
        if (start < 0)
            return QString();
        qsizetype end = headerBlock.indexOf('\n', start);
        if (end < 0)
            end = headerBlock.size();
        QString value =
            QString::fromUtf8(headerBlock.mid(start + prefix.size(), end - start - prefix.size()));
        value.remove(QLatin1String("\r"));
        while (value.startsWith(QLatin1Char(' ')))
            value.remove(0, 1);
        return value.trimmed();
    };
    QString out;
    const QString subject = headerValue("Subject");
    const QString from = headerValue("From");
    const QString to = headerValue("To");
    const QString date = headerValue("Date");
    if (!subject.isEmpty())
        out += QStringLiteral("Subject: %1\n").arg(subject);
    if (!from.isEmpty())
        out += QStringLiteral("From: %1\n").arg(from);
    if (!to.isEmpty())
        out += QStringLiteral("To: %1\n").arg(to);
    if (!date.isEmpty())
        out += QStringLiteral("Date: %1\n").arg(date);
    if (!out.isEmpty())
        out += QLatin1Char('\n');
    const bool isHtml =
        headerValue("Content-Type").contains(QLatin1String("text/html"), Qt::CaseInsensitive);
    const QString bodyText =
        isHtml ? ocapi::TextExtractor::htmlToText(body) : QString::fromUtf8(body);
    out += bodyText;
    return out;
}

QString TextExtractor::extractFromOffice(const QString &path, QString *error) {
#if OCAPI_HAVE_LIBARCHIVE
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QObject::tr("File does not exist: %1").arg(path);
        return {};
    }
    archive *arch = archive_read_new();
    if (!arch) {
        if (error)
            *error = QObject::tr("Could not open archive: %1").arg(QFileInfo(path).fileName());
        return {};
    }
    archive_read_support_format_zip(arch);
    archive_read_support_format_empty(arch);
    struct ArchiveGuard {
        archive *handle;
        ~ArchiveGuard() {
            if (handle)
                archive_read_free(handle);
        }
    } guard{arch};

    if (archive_read_open_filename(arch, path.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        if (error)
            *error =
                QObject::tr("Could not open Office document: %1 (%2)")
                    .arg(QFileInfo(path).fileName(), QString::fromUtf8(archive_error_string(arch)));
        return {};
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    QString extracted;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(arch, &entry) == ARCHIVE_OK) {
        const QString entryName = QString::fromUtf8(archive_entry_pathname_utf8(entry));
        QByteArray payload;
        bool wanted = false;
        bool odfMode = false;
        if (suffix == QLatin1String("docx")) {
            wanted = entryName == QLatin1String("word/document.xml");
        } else if (suffix == QLatin1String("xlsx")) {
            wanted = entryName == QLatin1String("xl/sharedStrings.xml") ||
                     entryName.startsWith(QLatin1String("xl/worksheets/sheet"));
        } else if (suffix == QLatin1String("pptx")) {
            wanted = entryName.startsWith(QLatin1String("ppt/slides/slide"));
        } else if (suffix == QLatin1String("odt") || suffix == QLatin1String("ods") ||
                   suffix == QLatin1String("odp")) {
            // OpenDocument keeps all text content in content.xml.
            wanted = entryName == QLatin1String("content.xml");
            odfMode = true;
        }
        if (!wanted)
            continue;
        payload.reserve(static_cast<int>(archive_entry_size(entry)));
        const void *chunk = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(arch, &chunk, &size, &offset) == ARCHIVE_OK)
            payload.append(static_cast<const char *>(chunk), static_cast<int>(size));
        if (payload.isEmpty())
            continue;
        const QString part = extractXmlText(payload, odfMode);
        if (!part.isEmpty())
            extracted += part + "\n";
    }
    if (extracted.isEmpty() && error)
        *error =
            QObject::tr("No text found in Office document: %1").arg(QFileInfo(path).fileName());
    return extracted;
#else
    Q_UNUSED(path);
    if (error)
        *error = QObject::tr(
            "Office document support is not available in this build (libarchive is missing).");
    return {};
#endif
}

QString TextExtractor::extractFromEmail(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Could not read file: %1").arg(path);
        return {};
    }
    const QByteArray raw = file.readAll();
    file.close();
    const bool isMbox = raw.startsWith("From ");
    QString extracted;
    if (isMbox) {
        // mbox: messages separated by lines starting with "From ".
        qsizetype pos = 0;
        while (pos < raw.size()) {
            qsizetype next = raw.indexOf("\nFrom ", pos + 1);
            if (next < 0)
                next = raw.size();
            // skip the mbox "From " line itself
            qsizetype lineEnd = raw.indexOf('\n', pos);
            if (lineEnd < 0)
                lineEnd = raw.size();
            const QByteArray message = raw.mid(lineEnd + 1, next - lineEnd - 1);
            const QString text = emailMessageToText(message);
            if (!text.isEmpty())
                extracted += text + "\n\n";
            pos = next;
        }
    } else {
        extracted = emailMessageToText(raw);
    }
    if (extracted.isEmpty() && error)
        *error = QObject::tr("No e-mail content found in: %1").arg(QFileInfo(path).fileName());
    return extracted;
}

#if OCAPI_HAVE_LIBARCHIVE
QString TextExtractor::extractFromArchive(const QString &path, QString *error) {
    archive *arch = archive_read_new();
    if (!arch) {
        if (error)
            *error = QObject::tr("Could not open archive: %1").arg(QFileInfo(path).fileName());
        return {};
    }
    archive_read_support_format_all(arch);
    archive_read_support_filter_all(arch);
    struct ArchiveGuard {
        archive *handle;
        ~ArchiveGuard() {
            if (handle)
                archive_read_free(handle);
        }
    } guard{arch};

    if (archive_read_open_filename(arch, path.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        if (error)
            *error =
                QObject::tr("Could not open archive: %1 (%2)")
                    .arg(QFileInfo(path).fileName(), QString::fromUtf8(archive_error_string(arch)));
        return {};
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        if (error)
            *error = QObject::tr("Could not create a temporary directory for archive extraction.");
        return {};
    }

    QString extracted;
    int entryCount = 0;
    qint64 totalBytes = 0;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(arch, &entry) == ARCHIVE_OK) {
        if (++entryCount > maxArchiveEntries()) {
            if (error)
                *error = QObject::tr("Archive has too many entries (limit: %1): %2")
                             .arg(maxArchiveEntries())
                             .arg(QFileInfo(path).fileName());
            return {};
        }
        const char *entryNameRaw = archive_entry_pathname(entry);
        if (!entryNameRaw)
            continue;
        const QString entryName = QString::fromUtf8(entryNameRaw);
        // Reject absolute paths and ".." traversal for safe extraction.
        if (entryName.startsWith(QLatin1Char('/')) || entryName.contains(QLatin1String("..")))
            continue;
        const la_int64_t entrySize = archive_entry_size(entry);
        if (entrySize < 0)
            continue;
        totalBytes += entrySize;
        if (totalBytes > maxArchiveTotalBytes()) {
            if (error)
                *error = QObject::tr("Archive expands beyond the size limit of %1 MB: %2")
                             .arg(maxArchiveTotalBytes() / (1024 * 1024))
                             .arg(QFileInfo(path).fileName());
            return {};
        }
        const QString targetPath = tempDir.path() + QLatin1Char('/') + entryName;
        QDir().mkpath(QFileInfo(targetPath).absolutePath());
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly))
            continue;
        const void *chunk = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        bool writeFailed = false;
        while (archive_read_data_block(arch, &chunk, &size, &offset) == ARCHIVE_OK) {
            if (out.write(static_cast<const char *>(chunk), static_cast<qint64>(size)) <
                static_cast<qint64>(size)) {
                writeFailed = true;
                break;
            }
        }
        out.close();
        if (writeFailed)
            continue;
        QString entryError;
        const QString text = extractFromFile(targetPath, &entryError);
        if (!text.isEmpty())
            extracted +=
                QStringLiteral("[Archive entry: %1]\n%2\n\n").arg(entryName, text.trimmed());
    }
    if (extracted.isEmpty() && error)
        *error =
            QObject::tr("No readable content found in archive: %1").arg(QFileInfo(path).fileName());
    return extracted;
}
#else
QString TextExtractor::extractFromArchive(const QString &path, QString *error) {
    Q_UNUSED(path);
    if (error)
        *error =
            QObject::tr("Archive support is not available in this build (libarchive is missing).");
    return {};
}
#endif

QString TextExtractor::extractFromFile(const QString &path, QString *error) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return extractFromPdf(path, error);
    if (suffix == QLatin1String("eml") || suffix == QLatin1String("mbox"))
        return extractFromEmail(path, error);
    if (hasLibArchive() && (suffix == QLatin1String("zip") || suffix == QLatin1String("tar") ||
                            suffix == QLatin1String("gz") || suffix == QLatin1String("tgz") ||
                            suffix == QLatin1String("bz2") || suffix == QLatin1String("xz") ||
                            suffix == QLatin1String("7z")))
        return extractFromArchive(path, error);
    if (hasLibArchive() && (suffix == QLatin1String("docx") || suffix == QLatin1String("xlsx") ||
                            suffix == QLatin1String("pptx") || suffix == QLatin1String("odt") ||
                            suffix == QLatin1String("ods") || suffix == QLatin1String("odp")))
        return extractFromOffice(path, error);
    if (hasTesseract()) {
        static const QStringList imageSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                  QStringLiteral("png"), QStringLiteral("webp"),
                                                  QStringLiteral("tif"), QStringLiteral("tiff"),
                                                  QStringLiteral("bmp"), QStringLiteral("gif")};
        if (imageSuffixes.contains(suffix))
            return extractFromImageFile(path, error);
    }
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QObject::tr("Could not read file: %1").arg(path);
            return {};
        }
        const QByteArray raw = file.readAll();
        file.close();
        const QString text = htmlToText(raw);
        if (text.isEmpty() && error)
            *error = QObject::tr("No readable text found in HTML file: %1")
                         .arg(QFileInfo(path).fileName());
        return text;
    }
    if (isTextFile(path))
        return extractFromTextFile(path, error);
    if (error)
        *error = QObject::tr("Unsupported file type for text extraction: %1")
                     .arg(QFileInfo(path).fileName());
    return {};
}

} // namespace ocapi
