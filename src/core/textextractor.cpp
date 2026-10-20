#include "core/textextractor.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStringDecoder>
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

// Text runs in OOXML payload XML live in <w:t> (Word), <t> (Excel shared
// strings and sheet cells) and <a:t> (PowerPoint) elements. Paragraph ends
// are marked with <w:p>/<a:p>; Excel cell values are separated by tabs.
QString extractXmlText(QByteArray xml, bool slideMode) {
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
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (inText)
                currentText += xmlReader.text();
        } else if (token == QXmlStreamReader::EndElement) {
            if (inText && (name == QLatin1String("t") || name == QLatin1String("w:t") ||
                           name == QLatin1String("a:t"))) {
                inText = false;
                if (!currentText.isEmpty()) {
                    if (!out.isEmpty())
                        out += slideMode ? QString(QLatin1Char('\n')) : QString(QLatin1Char(' '));
                    out += currentText;
                }
            }
        }
    }
    return out;
}

} // namespace
#endif

QString TextExtractor::extractFromOoxml(const QString &path, QString *error) {
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
        bool slideMode = false;
        if (suffix == QLatin1String("docx")) {
            wanted = entryName == QLatin1String("word/document.xml");
        } else if (suffix == QLatin1String("xlsx")) {
            wanted = entryName == QLatin1String("xl/sharedStrings.xml") ||
                     entryName.startsWith(QLatin1String("xl/worksheets/sheet"));
        } else if (suffix == QLatin1String("pptx")) {
            wanted = entryName.startsWith(QLatin1String("ppt/slides/slide"));
            slideMode = true;
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
        const QString part = extractXmlText(payload, slideMode);
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

QString TextExtractor::extractFromFile(const QString &path, QString *error) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return extractFromPdf(path, error);
    if (hasLibArchive() && (suffix == QLatin1String("docx") || suffix == QLatin1String("xlsx") ||
                            suffix == QLatin1String("pptx")))
        return extractFromOoxml(path, error);
    if (hasTesseract()) {
        static const QStringList imageSuffixes = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                  QStringLiteral("png"), QStringLiteral("webp"),
                                                  QStringLiteral("tif"), QStringLiteral("tiff"),
                                                  QStringLiteral("bmp"), QStringLiteral("gif")};
        if (imageSuffixes.contains(suffix))
            return extractFromImageFile(path, error);
    }
    if (isTextFile(path))
        return extractFromTextFile(path, error);
    if (error)
        *error = QObject::tr("Unsupported file type for text extraction: %1")
                     .arg(QFileInfo(path).fileName());
    return {};
}

} // namespace ocapi
