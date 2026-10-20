#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace ocapi {

// Extracts text from input files for the chat: text from PDF files via
// Poppler (with automatic OCR of scanned pages via Tesseract) and text from
// image files (e.g. scanned documents) via Tesseract. Both features are
// optional at build time; availability is reported by hasPoppler() and
// hasTesseract().
class TextExtractor {
public:
    [[nodiscard]] static bool hasPoppler();
    [[nodiscard]] static bool hasTesseract();

    [[nodiscard]] static QStringList supportedMimeTypes();

    // Extracts the text of a PDF file. Pages without embedded text (scanned
    // pages) are rendered and passed to OCR when Tesseract is available.
    [[nodiscard]] static QString extractFromPdf(const QString &path, QString *error = nullptr);

    // Recognizes text in an image (e.g. a scanned document) using Tesseract.
    [[nodiscard]] static QString extractFromImage(const QImage &image, QString *error = nullptr);

    // Extracts the text of an image file supported by Qt image plugins.
    [[nodiscard]] static QString extractFromImageFile(const QString &path,
                                                      QString *error = nullptr);

    // Extracts text from any supported file (PDF or image). Returns a
    // nullopt-like empty result with \p error set for unsupported inputs.
    [[nodiscard]] static QString extractFromFile(const QString &path, QString *error = nullptr);
};

} // namespace ocapi
