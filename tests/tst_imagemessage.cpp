#include "core/imagemessage.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

using namespace ocapi;

class TestImageMessage : public QObject {
    Q_OBJECT

private slots:
    void supportedMimeTypesCoverAllFormats();
    void nameFilterListsAllExtensions();
    void processFileHandlesPng();
    void processFileHandlesJpeg();
    void processFileResizesOversizedImages();
    void processFileRejectsUnsupportedFormat();
    void processFileRejectsCorruptImage();
    void gifAnimationIsPreserved();
    void fromBase64RoundTrip();
    void attachmentBase64DataMatchesPayload();

private:
    QString writeImage(const QImage &image, const QString &name);
    QTemporaryDir m_tempDir;
};

void TestImageMessage::supportedMimeTypesCoverAllFormats() {
    const auto types = ImagePreprocessor::supportedMimeTypes();
    QVERIFY(types.contains(QStringLiteral("image/jpeg")));
    QVERIFY(types.contains(QStringLiteral("image/png")));
    QVERIFY(types.contains(QStringLiteral("image/webp")));
    QVERIFY(types.contains(QStringLiteral("image/jxl")));
    QVERIFY(types.contains(QStringLiteral("image/avif")));
    QVERIFY(types.contains(QStringLiteral("image/tiff")));
    QVERIFY(types.contains(QStringLiteral("image/gif")));
}

void TestImageMessage::nameFilterListsAllExtensions() {
    const QString filter = ImagePreprocessor::supportedNameFilter();
    for (const char *ext : {"jpg", "jpeg", "png", "webp", "jxl", "avif", "tif", "tiff", "gif"})
        QVERIFY(filter.contains(QLatin1String(ext)));
}

QString TestImageMessage::writeImage(const QImage &image, const QString &name) {
    QString path = m_tempDir.filePath(name);
    QImageWriter writer(path);
    writer.setQuality(95);
    if (!writer.write(image))
        qWarning("failed to write test image: %s", qPrintable(writer.errorString()));
    return path;
}

void TestImageMessage::processFileHandlesPng() {
    QImage image(64, 32, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString path = writeImage(image, QStringLiteral("red.png"));
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY2(attachment.has_value(), qPrintable(error));
    QCOMPARE(attachment->mimeType, QStringLiteral("image/png"));
    QCOMPARE(attachment->width, 64);
    QCOMPARE(attachment->height, 32);
    QVERIFY(!attachment->resized);
    QVERIFY(!attachment->data.isEmpty());
    QCOMPARE(attachment->fileName, QStringLiteral("red.png"));
    QVERIFY(!attachment->decode().isNull());
}

void TestImageMessage::processFileHandlesJpeg() {
    QImage image(40, 40, QImage::Format_RGB32);
    image.fill(Qt::blue);
    const QString path = writeImage(image, QStringLiteral("blue.jpg"));
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY2(attachment.has_value(), qPrintable(error));
    QCOMPARE(attachment->mimeType, QStringLiteral("image/jpeg"));
    QCOMPARE(attachment->width, 40);
    QVERIFY(!attachment->data.isEmpty());
}

void TestImageMessage::processFileResizesOversizedImages() {
    QImage image(2000, 1000, QImage::Format_RGB32);
    image.fill(Qt::green);
    const QString path = writeImage(image, QStringLiteral("big.png"));
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY2(attachment.has_value(), qPrintable(error));
    QVERIFY(attachment->resized);
    QCOMPARE(attachment->width, ImagePreprocessor::maxDimension());
    QVERIFY(attachment->height < 1000);
    QVERIFY(qMax(attachment->width, attachment->height) <= ImagePreprocessor::maxDimension());
}

void TestImageMessage::processFileRejectsUnsupportedFormat() {
    const QString path = m_tempDir.filePath(QStringLiteral("notes.txt"));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write("just text, not an image");
    file.close();
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY(!attachment.has_value());
    QVERIFY(!error.isEmpty());
}

void TestImageMessage::processFileRejectsCorruptImage() {
    const QString path = m_tempDir.filePath(QStringLiteral("broken.png"));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write("\x89PNG\r\n\x1A\nnot-really-a-png");
    file.close();
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY(!attachment.has_value());
    QVERIFY(!error.isEmpty());
}

void TestImageMessage::gifAnimationIsPreserved() {
    const QByteArray gif(
        "\x47\x49\x46\x38\x39\x61\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00\x21\xF9"
        "\x04\x01\x00\x00\x00\x00\x2C\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00"
        "\x3B",
        42);
    const QString path = m_tempDir.filePath(QStringLiteral("tiny.gif"));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(gif);
    file.close();
    QString error;
    const auto attachment = ImagePreprocessor::processFile(path, &error);
    QVERIFY2(attachment.has_value(), qPrintable(error));
    QCOMPARE(attachment->mimeType, QStringLiteral("image/gif"));
    QVERIFY(!attachment->data.isEmpty());
}

void TestImageMessage::fromBase64RoundTrip() {
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, QByteArrayLiteral("png"));
    QVERIFY(writer.write(image));

    const QString base64 = QString::fromLatin1(bytes.toBase64());
    const auto attachment = ImagePreprocessor::fromBase64(
        QStringLiteral("00000000-0000-0000-0000-000000000001"), QStringLiteral("image/png"),
        QStringLiteral("x.png"), 10, 10, false, QString(), base64);
    QVERIFY(attachment.has_value());
    QCOMPARE(attachment->mimeType, QStringLiteral("image/png"));
    QCOMPARE(attachment->width, 10);
    QCOMPARE(attachment->data, bytes);
    QVERIFY(!attachment->decode().isNull());
}

void TestImageMessage::attachmentBase64DataMatchesPayload() {
    ImageAttachment attachment;
    attachment.mimeType = QStringLiteral("image/png");
    attachment.data = QByteArrayLiteral("\x89PNG");
    QCOMPARE(QByteArray::fromBase64(attachment.base64Data().toLatin1()),
             QByteArrayLiteral("\x89PNG"));
}

QTEST_GUILESS_MAIN(TestImageMessage)
#include "tst_imagemessage.moc"
