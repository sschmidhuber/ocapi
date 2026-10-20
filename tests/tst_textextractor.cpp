#include "core/textextractor.h"

#include <QtTest>

#include <array>
#include <numeric>

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

namespace {

// clang-format off
QByteArray docxZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAIAJWQVF3uR1hmHwAAAB0AAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbLOxr8jNUShL") +
        QByteArrayLiteral("LSrOzM+zVTLUM1Cyt7MJqSxILda3AwBQSwMEFAAAAAgAlZBUXU5LWUmfAAAAFAEAABEAAAB3b3Jk") +
        QByteArrayLiteral("L2RvY3VtZW50LnhtbHWPSw7CMAxErxLlALiwYFH1s2XPCUwS2khJHDmBltuTSCCKBJuxR08ejbtx") +
        QByteArrayLiteral("9U7cDSdLoZf7XSPHoVtaTermTcii4JDapZdzzrEFSGo2HtOOogmFXYk95mJ5goVYRyZlUrJh8g4O") +
        QByteArrayLiteral("TXMEjzbIGnkh/agzVuEqeTgZ50h0UPeqvGGkMNo/rNRbvwi8crfhZ6MoaBGRcWKM848DeNeCz8vD") +
        QByteArrayLiteral("E1BLAQIUAxQAAAAIAJWQVF3uR1hmHwAAAB0AAAATAAAAAAAAAAAAAACAAQAAAABbQ29udGVudF9U") +
        QByteArrayLiteral("eXBlc10ueG1sUEsBAhQDFAAAAAgAlZBUXU5LWUmfAAAAFAEAABEAAAAAAAAAAAAAAIABUAAAAHdv") +
        QByteArrayLiteral("cmQvZG9jdW1lbnQueG1sUEsFBgAAAAACAAIAgAAAAB4BAAAAAA=="));
}
// clang-format on

// clang-format off
QByteArray xlsxZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAIAJWQVF3uR1hmHwAAAB0AAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbLOxr8jNUShL") +
        QByteArrayLiteral("LSrOzM+zVTLUM1Cyt7MJqSxILda3AwBQSwMEFAAAAAgAlZBUXd7JrhdxAAAAiwAAABQAAAB4bC9z") +
        QByteArrayLiteral("aGFyZWRTdHJpbmdzLnhtbE2NMQ7CMAxFr1LlAHVgYEBuegNGdosaEilOothFHJ+gLozvfel9XD+S") +
        QByteArrayLiteral("pzd3TbUs7jR7twZUtWn4oouLZu0KoI/IQjrXxmUsz9qFbGB/gbbOtGlkNslw9v4CQqm4kUkBLdxI") +
        QByteArrayLiteral("GMECwo8Pd6e8/0kYh+ELUEsDBBQAAAAIAJWQVF0Vy+O4gAAAALoAAAAYAAAAeGwvd29ya3NoZWV0") +
        QByteArrayLiteral("cy9zaGVldDEueG1sVY5LDsIwDESvUuUAdcqCBXLcDReJgiEVzUdxlHJ8oiwqWPq9mZFx/YR9alxk") +
        QByteArrayLiteral("S9GoZdZqJTxSeYtnrlO3UYzyteYbgDjPwcqcMsdunqkEW/tZXiC5sH2MUtjhovUVgt2iIhzsbqsl") +
        QByteArrayLiteral("LOkgdFM1SrpopBEaIbg/uJwQRh5+BuD8jL5QSwECFAMUAAAACACVkFRd7kdYZh8AAAAdAAAAEwAA") +
        QByteArrayLiteral("AAAAAAAAAAAAgAEAAAAAW0NvbnRlbnRfVHlwZXNdLnhtbFBLAQIUAxQAAAAIAJWQVF3eya4XcQAA") +
        QByteArrayLiteral("AIsAAAAUAAAAAAAAAAAAAACAAVAAAAB4bC9zaGFyZWRTdHJpbmdzLnhtbFBLAQIUAxQAAAAIAJWQ") +
        QByteArrayLiteral("VF0Vy+O4gAAAALoAAAAYAAAAAAAAAAAAAACAAfMAAAB4bC93b3Jrc2hlZXRzL3NoZWV0MS54bWxQ") +
        QByteArrayLiteral("SwUGAAAAAAMAAwDJAAAAqQEAAAAA"));
}
// clang-format on

// clang-format off
QByteArray pptxZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAIAJWQVF3uR1hmHwAAAB0AAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbLOxr8jNUShL") +
        QByteArrayLiteral("LSrOzM+zVTLUM1Cyt7MJqSxILda3AwBQSwMEFAAAAAgAlZBUXbDh0OqmAAAAIAEAABUAAABwcHQv") +
        QByteArrayLiteral("c2xpZGVzL3NsaWRlMS54bWyNkE0OwiAQha/ScIBOdeGCUJp4hXoBUsaWhL/AROvthVbTuHPzBebx") +
        QByteArrayLiteral("3rwghtXZ5oEpm+B7dmo7NkgReba6KYrPXPVsIYocIE8LOpXbENEX7R6SU1SuaQad1NP42Vk4d90F") +
        QByteArrayLiteral("nDKeffzxH39MmNGTotLiJ6R2mUart07xlhD3UyWt16BfUigeK1IFydEajU3w2BCuJKDOKtPG8hIO") +
        QByteArrayLiteral("J+xRcGTDdx1sfyDfUEsDBBQAAAAIAJWQVF1O3XE0pwAAACABAAAVAAAAcHB0L3NsaWRlcy9zbGlk") +
        QByteArrayLiteral("ZTIueG1sjZBNDsIgEIWv0nCATnXhglCaeAW9ACljS8JfYGLr7YVW07hz8wXm8d68IIbV2eaJKZvg") +
        QByteArrayLiteral("e3ZqOzZIEXm2uimKz1z1bCaKHCCPMzqV2xDRF+0RklNUrmkCndRi/OQsnLvuAk4Zzz7++I8/Jszo") +
        QByteArrayLiteral("SVFp8RNSu4w3q7dO8Z4Q91MlrdegX1IoHitSBcmbNRobWkJDuJKAOqtMG8tLOJywR8GRDd91sP2B") +
        QByteArrayLiteral("fANQSwECFAMUAAAACACVkFRd7kdYZh8AAAAdAAAAEwAAAAAAAAAAAAAAgAEAAAAAW0NvbnRlbnRf") +
        QByteArrayLiteral("VHlwZXNdLnhtbFBLAQIUAxQAAAAIAJWQVF2w4dDqpgAAACABAAAVAAAAAAAAAAAAAACAAVAAAABw") +
        QByteArrayLiteral("cHQvc2xpZGVzL3NsaWRlMS54bWxQSwECFAMUAAAACACVkFRdTt1xNKcAAAAgAQAAFQAAAAAAAAAA") +
        QByteArrayLiteral("AAAAgAEpAQAAcHB0L3NsaWRlcy9zbGlkZTIueG1sUEsFBgAAAAADAAMAxwAAAAMCAAAAAA=="));
}
// clang-format on

} // namespace

class TestTextExtractor : public QObject {
    Q_OBJECT
private slots:
    void availabilityFlagsAreConsistent();
    void extractFromPdfReturnsEmbeddedText();
    void extractFromPdfRejectsMissingFile();
    void extractFromImageIsAvailableOrReportsError();
    void extractFromFileRejectsUnsupportedSuffix();
    void extractFromTextFileReturnsContent();
    void extractFromTextFileTruncatesLargeFiles();
    void isTextFileAcceptsPlainTextFormats();
    void isTextFileRejectsBinaryFiles();
    void extractFromFileRoutesTextFiles();
    void extractFromOoxmlHandlesDocx();
    void extractFromOoxmlHandlesXlsx();
    void extractFromOoxmlHandlesPptx();
    void extractFromOoxmlRejectsMissingFile();
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

void TestTextExtractor::extractFromTextFileReturnsContent() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("name,value\nfoo,42\nbar,1337");
    file.close();

    QString error;
    const QString text = TextExtractor::extractFromTextFile(path, &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));
    QCOMPARE(text, QStringLiteral("name,value\nfoo,42\nbar,1337"));
    QFile::remove(path);
}

void TestTextExtractor::extractFromTextFileTruncatesLargeFiles() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-large.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QString filler = QString(TextExtractor::maxTextFileChars() + 100, QLatin1Char('x'));
    file.write(filler.toUtf8());
    file.close();

    QString error;
    const QString text = TextExtractor::extractFromTextFile(path, &error);
    QVERIFY(text.size() <= TextExtractor::maxTextFileChars() + 100);
    QVERIFY(text.contains(QStringLiteral("[... file truncated ...]")));
    QFile::remove(path);
}

void TestTextExtractor::isTextFileAcceptsPlainTextFormats() {
    struct TestCase {
        QString name;
        QByteArray content;
    };
    const std::array<TestCase, 6> cases = {
        TestCase{QStringLiteral("ocapi.csv"), QByteArrayLiteral("id,name\n1,alpha\n2,beta")},
        TestCase{QStringLiteral("ocapi.md"), QByteArrayLiteral("# Heading\n\nSome *markdown*.")},
        TestCase{QStringLiteral("ocapi.html"), QByteArrayLiteral("<html><body>hi</body></html>\n")},
        TestCase{QStringLiteral("ocapi.cpp"), QByteArrayLiteral("int main() { return 0; }\n")},
        TestCase{QStringLiteral("ocapi.json"), QByteArrayLiteral("{\"key\": [1, 2, 3]}")},
        TestCase{QStringLiteral("ocapi.log"), QByteArrayLiteral("2026-10-20 INFO started\n")}};
    for (const auto &testCase : cases) {
        const QString path = QDir::temp().filePath(testCase.name);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(testCase.name));
        file.write(testCase.content);
        file.close();
        QVERIFY2(TextExtractor::isTextFile(path), qPrintable(testCase.name));
        QVERIFY2(!TextExtractor::extractFromFile(path).isEmpty(), qPrintable(testCase.name));
        QFile::remove(path);
    }
}

void TestTextExtractor::isTextFileRejectsBinaryFiles() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-binary.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    std::array<char, 64> binary{};
    std::iota(binary.begin(), binary.end(), static_cast<char>(0));
    file.write(binary.data(), static_cast<qint64>(binary.size()));
    file.close();
    QVERIFY(!TextExtractor::isTextFile(path));

    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
    QFile::remove(path);
}

void TestTextExtractor::extractFromFileRoutesTextFiles() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-notes.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("- first\n- second");
    file.close();

    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    QCOMPARE(text, QStringLiteral("- first\n- second"));
    QFile::remove(path);
}

void writeFixture(const QString &path, const QByteArray &zip) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(zip);
    file.close();
}

void TestTextExtractor::extractFromOoxmlHandlesDocx() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");

    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.docx"));
    writeFixture(path, docxZip());
    QString error;
    const QString text = TextExtractor::extractFromOoxml(path, &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));
    QVERIFY(text.contains(QStringLiteral("Hello"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("ocapi"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("docx"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("Second paragraph"), Qt::CaseInsensitive));
    QFile::remove(path);
}

void TestTextExtractor::extractFromOoxmlHandlesXlsx() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");

    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.xlsx"));
    writeFixture(path, xlsxZip());
    QString error;
    const QString text = TextExtractor::extractFromOoxml(path, &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));
    QVERIFY(text.contains(QStringLiteral("Name"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("Value"), Qt::CaseInsensitive));
    QFile::remove(path);
}

void TestTextExtractor::extractFromOoxmlHandlesPptx() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");

    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.pptx"));
    writeFixture(path, pptxZip());
    QString error;
    const QString text = TextExtractor::extractFromOoxml(path, &error);
    QVERIFY2(!text.isEmpty(), qPrintable(error));
    QVERIFY(text.contains(QStringLiteral("Slide one text"), Qt::CaseInsensitive));
    QVERIFY(text.contains(QStringLiteral("Slide two text"), Qt::CaseInsensitive));
    QFile::remove(path);
}

void TestTextExtractor::extractFromOoxmlRejectsMissingFile() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");

    QString error;
    const QString text = TextExtractor::extractFromOoxml(
        QDir::temp().filePath(QStringLiteral("ocapi-missing-file.docx")), &error);
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestTextExtractor::extractFromFileRejectsUnsupportedSuffix() {
    const QString path = QDir::temp().filePath(QStringLiteral("ocapi-test.xyz"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    std::array<char, 48> binary{};
    std::iota(binary.begin(), binary.end(), static_cast<char>(0));
    file.write(binary.data(), static_cast<qint64>(binary.size()));
    file.close();
    QString error;
    const QString text = TextExtractor::extractFromFile(path, &error);
    QVERIFY(text.isEmpty());
    QVERIFY(!error.isEmpty());
    QFile::remove(path);
}

QTEST_GUILESS_MAIN(TestTextExtractor)
#include "tst_textextractor.moc"
