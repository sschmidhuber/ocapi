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
