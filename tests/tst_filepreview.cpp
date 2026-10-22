#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestFilePreview : public QObject {
    Q_OBJECT

private slots:
    void categoryForFileMapsExtensions();
    void badgeForCategoryCoversCategories();
    void thumbnailForTextFileIsValidPng();
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

} // namespace

void TestFilePreview::thumbnailForTextFileIsValidPng() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello world");
    file.close();

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QVERIFY(qMax(image.width(), image.height()) <= 64);
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

QTEST_MAIN(TestFilePreview)
#include "tst_filepreview.moc"
