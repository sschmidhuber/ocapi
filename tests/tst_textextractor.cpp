#include "core/textextractor.h"

#include <QtTest>

#include <array>

using namespace ocapi;

namespace {

void writeMinimalPdf(const QString &path, const QByteArray &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qWarning("failed to write test pdf");
    file.write(content);
    file.close();
}

// A minimal but valid single-page PDF containing the text "Hello ocapi PDF".
QByteArray minimalPdf() {
    const std::array<QByteArray, 5> objects = {
        QByteArrayLiteral("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"),
        QByteArrayLiteral("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"),
        QByteArrayLiteral("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
                          "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>\nendobj\n"),
        QByteArrayLiteral("4 0 obj\n<< /Length 46 >>\nstream\nBT /F1 24 Tf 20 40 Td "
                          "(Hello ocapi PDF) Tj ET\nendstream\nendobj\n"),
        QByteArrayLiteral("5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                          ">>\nendobj\n")};
    QByteArray pdf("%PDF-1.4\n");
    QList<int> offsets;
    for (const QByteArray &object : objects) {
        offsets.push_back(static_cast<int>(pdf.size()));
        pdf += object;
    }
    const int xrefPos = static_cast<int>(pdf.size());
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    for (int offset : offsets)
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefPos) +
           "\n%%EOF\n";
    return pdf;
}

} // namespace

class TestTextExtractor : public QObject {
    Q_OBJECT
private slots:
    void availabilityFlagsAreConsistent();
    void extractFromPdfReturnsEmbeddedText();
    void extractFromPdfRejectsMissingFile();
    void extractFromImageIsAvailableOrReportsError();
    void extractFromFileRejectsUnsupportedSuffix();
};

void TestTextExtractor::availabilityFlagsAreConsistent() {
    if (TextExtractor::hasPoppler())
        QVERIFY(!TextExtractor::supportedMimeTypes().isEmpty());
    else
        QVERIFY(!TextExtractor::supportedMimeTypes().contains(QStringLiteral("application/pdf")));
}

void TestTextExtractor::extractFromPdfReturnsEmbeddedText() {
    if (!TextExtractor::hasPoppler())
        QSKIP("poppler-cpp not available in this build");

    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.pdf"));
    writeMinimalPdf(path, minimalPdf());

    QString error;
    const QString text = TextExtractor::extractFromPdf(path, &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));
    QVERIFY(text.contains(QStringLiteral("Hello ocapi PDF"), Qt::CaseInsensitive));
    QFile::remove(path);
}

void TestTextExtractor::extractFromPdfRejectsMissingFile() {
    QString error;
    const QString text = TextExtractor::extractFromPdf(
        QDir::temp().filePath(QStringLiteral("ocapi-missing-file.pdf")), &error);
    QVERIFY(text.isEmpty());
    if (TextExtractor::hasPoppler())
        QVERIFY(!error.isEmpty());
}

void TestTextExtractor::extractFromImageIsAvailableOrReportsError() {
    if (!TextExtractor::hasTesseract())
        QSKIP("tesseract not available in this build");
    QImage image(64, 32, QImage::Format_RGB32);
    image.fill(Qt::white);
    QString error;
    const QString text = TextExtractor::extractFromImage(image, &error);
    QVERIFY(text.isEmpty() || !error.isEmpty() || text.trimmed().isEmpty());
}

void TestTextExtractor::extractFromFileRejectsUnsupportedSuffix() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.xyz"));
    writeMinimalPdf(path, QByteArrayLiteral("not really"));
    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
    QFile::remove(path);
}

QTEST_GUILESS_MAIN(TestTextExtractor)
#include "tst_textextractor.moc"
