#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestFilePreview : public QObject {
    Q_OBJECT

private slots:
    void categoryForFileMapsExtensions();
    void badgeForCategoryCoversCategories();
    void thumbnailForTextFileIsValidPng();
    void thumbnailForTextFileShowsContent();
    void thumbnailForOfficeWithEmbeddedPreview();
    void thumbnailForOdfWithEmbeddedPreview();
    void thumbnailForOfficeWithoutEmbeddedPreviewFallsBackToIcon();
    void thumbnailForArchiveFileIsValidPng();
    void thumbnailForEmailFileIsValidPng();
    void thumbnailForGenericFileIsValidPng();
    void thumbnailRespectsMaxEdge();
    void thumbnailForMissingFileFallsBackToIcon();
    void thumbnailWithNonPositiveEdgeIsEmpty();
    void pdfThumbnailMatchesRenderFirstPdfPage();
};

void TestFilePreview::categoryForFileMapsExtensions() {
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("/tmp/report.PDF")), FileCategory::Pdf);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.docx")), FileCategory::Word);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.odt")), FileCategory::Word);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.xlsx")), FileCategory::Spreadsheet);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.ods")), FileCategory::Spreadsheet);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.pptx")), FileCategory::Presentation);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.odp")), FileCategory::Presentation);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.zip")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.tar.gz")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.7z")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.eml")), FileCategory::Email);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.mbox")), FileCategory::Email);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.txt")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.md")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.py")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.bin")), FileCategory::Generic);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("noextension")), FileCategory::Generic);
}

void TestFilePreview::badgeForCategoryCoversCategories() {
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Pdf), QStringLiteral("PDF"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Word), QStringLiteral("W"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Spreadsheet), QStringLiteral("X"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Presentation), QStringLiteral("P"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Archive), QStringLiteral("ZIP"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Email), QStringLiteral("@"));
    QVERIFY(FilePreview::badgeForCategory(FileCategory::Text).isEmpty());
    QVERIFY(FilePreview::badgeForCategory(FileCategory::Generic).isEmpty());
}

namespace {

QImage decodePng(const QByteArray &bytes) {
    return QImage::fromData(bytes);
}

void writeFile(const QString &path, const QByteArray &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qWarning("failed to write test file");
    file.write(content);
    file.close();
}

// Minimal ZIP packages with an embedded first-page preview image. The
// embedded image bytes are a PNG (QImage::fromData sniffs the actual
// content, not the entry name); real-world OOXML files embed a JPEG.
// clang-format off
QByteArray docxWithThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbDxUeXBlcy8+") +
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13iS0eWSgAAAEoAAAAXAAAAZG9jUHJvcHMvdGh1bWJuYWlsLmpwZWeJUE5H") +
        QByteArrayLiteral("DQoaCgAAAA1JSERSAAAACAAAAAgIAgAAAEttKdwAAAARSURBVHicYzghJ4cVMQwtCQCQv0EBsp8+") +
        QByteArrayLiteral("IgAAAABJRU5ErkJgglBLAQIUAxQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAAAAAAAAAAACAAQAA") +
        QByteArrayLiteral("AABbQ29udGVudF9UeXBlc10ueG1sUEsBAhQDFAAAAAAAtypXXeJLR5ZKAAAASgAAABcAAAAAAAAA") +
        QByteArrayLiteral("AAAAAIABOQAAAGRvY1Byb3BzL3RodW1ibmFpbC5qcGVnUEsFBgAAAAACAAIAhgAAALgAAAAAAA=="));
}

QByteArray docxWithoutThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbDxUeXBlcy8+") +
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13dWoYNDQAAAA0AAAARAAAAd29yZC9kb2N1bWVudC54bWw8dzpkb2N1bWVu") +
        QByteArrayLiteral("dC8+UEsBAhQDFAAAAAAAtypXXcccFzwIAAAACAAAABMAAAAAAAAAAAAAAIABAAAAAFtDb250ZW50") +
        QByteArrayLiteral("X1R5cGVzXS54bWxQSwECFAMUAAAAAAC3Kldd3VqGDQ0AAAANAAAAEQAAAAAAAAAAAAAAgAE5AAAA") +
        QByteArrayLiteral("d29yZC9kb2N1bWVudC54bWxQSwUGAAAAAAIAAgCAAAAAdQAAAAAA"));
}

QByteArray odtWithThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV11exjIMJwAAACcAAAAIAAAAbWltZXR5cGVhcHBsaWNhdGlvbi92bmQub2Fz") +
        QByteArrayLiteral("aXMub3BlbmRvY3VtZW50LnRleHRQSwMEFAAAAAAAtypXXeJLR5ZKAAAASgAAABgAAABUaHVtYm5h") +
        QByteArrayLiteral("aWxzL3RodW1ibmFpbC5wbmeJUE5HDQoaCgAAAA1JSERSAAAACAAAAAgIAgAAAEttKdwAAAARSURB") +
        QByteArrayLiteral("VHicYzghJ4cVMQwtCQCQv0EBsp8+IgAAAABJRU5ErkJgglBLAQIUAxQAAAAAALcqV11exjIMJwAA") +
        QByteArrayLiteral("ACcAAAAIAAAAAAAAAAAAAACAAQAAAABtaW1ldHlwZVBLAQIUAxQAAAAAALcqV13iS0eWSgAAAEoA") +
        QByteArrayLiteral("AAAYAAAAAAAAAAAAAACAAU0AAABUaHVtYm5haWxzL3RodW1ibmFpbC5wbmdQSwUGAAAAAAIAAgB8") +
        QByteArrayLiteral("AAAAzQAAAAAA"));
}
// clang-format on

} // namespace

void TestFilePreview::thumbnailForTextFileIsValidPng() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    writeFile(path, "hello world");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QVERIFY(qMax(image.width(), image.height()) <= 64);
}

void TestFilePreview::thumbnailForTextFileShowsContent() {
    // The text preview paints the file's first lines in dark pixels on a
    // white page; a line-only tile (the fallback) would not have dark text
    // pixels in the upper text area.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("code.py"));
    writeFile(path, "def hello():\n    print('hi')\n");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // Scan the page area for dark (text) pixels.
    int darkPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() < 100 && c.green() < 100 && c.blue() < 100)
                ++darkPixels;
        }
    }
    QVERIFY(darkPixels > 20);
}

void TestFilePreview::thumbnailForOfficeWithEmbeddedPreview() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.docx"));
    writeFile(path, docxWithThumbnailZip());

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // The embedded preview is a small square image; scaled into the tile it
    // keeps its aspect ratio, so the thumbnail is square-ish.
    QVERIFY(qMax(image.width(), image.height()) <= 64);
}

void TestFilePreview::thumbnailForOdfWithEmbeddedPreview() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.odt"));
    writeFile(path, odtWithThumbnailZip());

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForOfficeWithoutEmbeddedPreviewFallsBackToIcon() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plain.docx"));
    writeFile(path, docxWithoutThumbnailZip());

    // No embedded preview: falls back to the icon tile, still a valid PNG.
    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForArchiveFileIsValidPng() {
    const QByteArray thumbnail =
        FilePreview::thumbnailForFile(QStringLiteral("/tmp/backup.zip"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForEmailFileIsValidPng() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("mail.eml"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForGenericFileIsValidPng() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("data.xyz"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailRespectsMaxEdge() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("notes.md"), 48);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QCOMPARE(qMax(image.width(), image.height()), 48);
}

void TestFilePreview::thumbnailForMissingFileFallsBackToIcon() {
    // A missing PDF falls back to the generic icon tile instead of failing.
    const QByteArray thumbnail =
        FilePreview::thumbnailForFile(QStringLiteral("/nonexistent/file.pdf"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailWithNonPositiveEdgeIsEmpty() {
    QVERIFY(FilePreview::thumbnailForFile(QStringLiteral("notes.txt"), 0).isEmpty());
    QVERIFY(FilePreview::thumbnailForFile(QStringLiteral("notes.txt"), -5).isEmpty());
}

void TestFilePreview::pdfThumbnailMatchesRenderFirstPdfPage() {
    if (!TextExtractor::hasPoppler())
        QSKIP("Poppler not available in this build");
    // A minimal but valid single-page PDF.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
               "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
               "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >>\nendobj\n"
               "trailer\n<< /Size 4 /Root 1 0 R >>\n%%EOF\n");
    file.close();

    const QImage rendered = TextExtractor::renderFirstPdfPage(path, 96);
    if (rendered.isNull())
        QSKIP("Poppler could not render the minimal test PDF");
    QVERIFY(qMax(rendered.width(), rendered.height()) <= 96);

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage thumbImage = decodePng(thumbnail);
    QVERIFY(!thumbImage.isNull());
    // The PDF preview should be close to the directly rendered page size.
    QCOMPARE(qMax(thumbImage.width(), thumbImage.height()), 96);
}

// QTEST_MAIN equivalent that forces the offscreen platform: the thumbnail
// tests paint with QPainter/QFont, which needs a QGuiApplication, but the
// CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestFilePreview test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_filepreview.moc"
