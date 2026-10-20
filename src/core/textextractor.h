#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace ocapi {

// Extracts text from input files for the chat: text from PDF files via
// Poppler (with automatic OCR of scanned pages via Tesseract), text from
// image files (e.g. scanned documents) via Tesseract, plain text files
// (CSV, Markdown, HTML, source code, ...), MS Office documents
// (.docx/.xlsx/.pptx OOXML packages) and OpenDocument documents
// (.odt/.ods/.odp ODF packages) via libarchive + QXmlStreamReader.
// Poppler, Tesseract and libarchive are optional at build time;
// availability is reported by hasPoppler(), hasTesseract() and
// hasLibArchive().
class TextExtractor {
public:
    [[nodiscard]] static bool hasPoppler();
    [[nodiscard]] static bool hasTesseract();
    [[nodiscard]] static bool hasLibArchive();

    [[nodiscard]] static QStringList supportedMimeTypes();

    // Extracts the text of a PDF file. Pages without embedded text (scanned
    // pages) are rendered and passed to OCR when Tesseract is available.
    [[nodiscard]] static QString extractFromPdf(const QString &path, QString *error = nullptr);

    // Recognizes text in an image (e.g. a scanned document) using Tesseract.
    [[nodiscard]] static QString extractFromImage(const QImage &image, QString *error = nullptr);

    // Extracts the text of an image file supported by Qt image plugins.
    [[nodiscard]] static QString extractFromImageFile(const QString &path,
                                                      QString *error = nullptr);

    // Returns true when the file looks like plain text: decodable as UTF-8
    // (or ASCII/latin fallback) and free of NUL bytes, with a printable
    // ratio above a threshold. Used to accept arbitrary text files such as
    // CSV, Markdown, HTML or source code regardless of their extension.
    [[nodiscard]] static bool isTextFile(const QString &path);

    // Extracts the content of a plain text file. Very large files are
    // truncated to maxTextFileChars().
    [[nodiscard]] static QString extractFromTextFile(const QString &path, QString *error = nullptr);

    // Extracts text from an office package (.docx/.xlsx/.pptx OOXML or
    // .odt/.ods/.odp OpenDocument) by reading the ZIP container with
    // libarchive and pulling the text out of the payload XML with
    // QXmlStreamReader.
    [[nodiscard]] static QString extractFromOffice(const QString &path, QString *error = nullptr);

    // Extracts text from any supported file (PDF, image, text file or
    // OOXML document). Returns an empty result with \p error set for
    // unsupported inputs.
    [[nodiscard]] static QString extractFromFile(const QString &path, QString *error = nullptr);

    [[nodiscard]] static int maxTextFileChars() { return 200000; }
};

} // namespace ocapi
