#include "core/imagemessage.h"

#include <array>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QUuid>

namespace ocapi {

namespace {

struct SignatureEntry {
    QByteArray signature;
    QString mimeType;
};

const std::array<SignatureEntry, 8> kSignatures{
    {QByteArrayLiteral("\xFF\xD8\xFF"), QStringLiteral("image/jpeg")},
    {QByteArrayLiteral("\x89PNG\r\n\x1A\n"), QStringLiteral("image/png")},
    {QByteArrayLiteral("II*\x0"), QStringLiteral("image/tiff")},
    {QByteArrayLiteral("MM\x0*"), QStringLiteral("image/tiff")},
    {QByteArrayLiteral("GIF87a"), QStringLiteral("image/gif")},
    {QByteArrayLiteral("GIF89a"), QStringLiteral("image/gif")},
    {QByteArrayLiteral("\xFF\x0A"), QStringLiteral("image/jxl")},
    {QByteArrayLiteral("\x0\x0\x0\xCJXL "), QStringLiteral("image/jxl")},
};

bool isWebP(const QByteArray &bytes) {
    return bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP";
}

bool isAvif(const QByteArray &bytes) {
    return bytes.size() >= 12 && bytes.mid(4, 4) == "ftyp" &&
           (bytes.mid(8, 4) == "avif" || bytes.mid(8, 4) == "avis");
}

// Providers widely accept JPEG, PNG, WebP and GIF; everything else is
// re-encoded (TIFF, JPEG XL, AVIF) after preprocessing.
bool providerFriendly(const QString &mimeType) {
    return mimeType == QLatin1String("image/jpeg") || mimeType == QLatin1String("image/png") ||
           mimeType == QLatin1String("image/webp") || mimeType == QLatin1String("image/gif");
}

QByteArray encodePng(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, QByteArrayLiteral("png"));
    return writer.write(image) ? bytes : QByteArray();
}

QByteArray encodeJpeg(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, QByteArrayLiteral("jpg"));
    writer.setQuality(90);
    return writer.write(image) ? bytes : QByteArray();
}

} // namespace

QStringList ImagePreprocessor::supportedMimeTypes() {
    return {QStringLiteral("image/jpeg"), QStringLiteral("image/png"),
            QStringLiteral("image/webp"), QStringLiteral("image/jxl"),
            QStringLiteral("image/avif"), QStringLiteral("image/tiff"),
            QStringLiteral("image/gif")};
}

QString ImagePreprocessor::supportedNameFilter() {
    return QStringLiteral("Images (*.jpg *.jpeg *.png *.webp *.jxl *.avif *.tif *.tiff *.gif)");
}

std::optional<QString> ImagePreprocessor::sniffMimeType(const QByteArray &bytes) {
    for (const auto &entry : kSignatures)
        if (bytes.startsWith(entry.signature))
            return entry.mimeType;
    if (isWebP(bytes))
        return QStringLiteral("image/webp");
    if (isAvif(bytes))
        return QStringLiteral("image/avif");
    return std::nullopt;
}

std::optional<ImageAttachment> ImagePreprocessor::processImage(const QImage &image,
                                                               const QString &mimeType,
                                                               const QString &fileName,
                                                               QString *error) {
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("Unsupported or corrupt image data: %1").arg(fileName);
        return std::nullopt;
    }

    QString encodedType;
    QByteArray bytes;
    if (mimeType == QLatin1String("image/jpeg")) {
        bytes = encodeJpeg(image);
        encodedType = QStringLiteral("image/jpeg");
    } else {
        bytes = encodePng(image);
        encodedType = QStringLiteral("image/png");
    }
    if (bytes.isEmpty()) {
        if (error)
            *error = QObject::tr("Could not encode image: %1").arg(fileName);
        return std::nullopt;
    }

    ImageAttachment attachment;
    attachment.mimeType = encodedType;
    attachment.fileName = fileName;
    attachment.sourcePath = fileName;
    attachment.width = image.width();
    attachment.height = image.height();
    attachment.data = bytes;
    return attachment;
}

std::optional<ImageAttachment> ImagePreprocessor::processFile(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Could not read file: %1").arg(path);
        return std::nullopt;
    }
    const QByteArray raw = file.readAll();
    file.close();

    const auto sniffed = sniffMimeType(raw);
    if (!sniffed) {
        if (error)
            *error = QObject::tr("Unsupported image format: %1").arg(QFileInfo(path).fileName());
        return std::nullopt;
    }
    const QString &mimeType = *sniffed;
    const QString fileName = QFileInfo(path).fileName();

    // GIFs: keep the original (possibly animated) bytes when within limits.
    if (mimeType == QLatin1String("image/gif")) {
        QImage probe;
        if (probe.loadFromData(raw, "gif")) {
            const int longest = qMax(probe.width(), probe.height());
            if (longest <= maxDimension()) {
                ImageAttachment attachment;
                attachment.mimeType = mimeType;
                attachment.fileName = fileName;
                attachment.sourcePath = path;
                attachment.width = probe.width();
                attachment.height = probe.height();
                attachment.data = raw;
                return attachment;
            }
        }
    }

    QBuffer buffer;
    buffer.setData(raw);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error =
                QObject::tr("Could not decode image: %1 (%2)").arg(fileName, reader.errorString());
        return std::nullopt;
    }

    const int longest = qMax(image.width(), image.height());
    if (longest <= maxDimension() && providerFriendly(mimeType)) {
        ImageAttachment attachment;
        attachment.mimeType = mimeType;
        attachment.fileName = fileName;
        attachment.sourcePath = path;
        attachment.width = image.width();
        attachment.height = image.height();
        attachment.data = raw;
        return attachment;
    }

    if (longest > maxDimension()) {
        image = image.scaled(maxDimension(), maxDimension(), Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    auto attachment = processImage(image, mimeType, fileName, error);
    if (attachment) {
        attachment->sourcePath = path;
        attachment->resized = longest > maxDimension();
    }
    return attachment;
}

std::optional<ImageAttachment>
ImagePreprocessor::fromBase64(const QString &id, const QString &mimeType, const QString &fileName,
                              int width, int height, bool resized, const QString &sourcePath,
                              const QString &base64) {
    const QByteArray decoded = QByteArray::fromBase64(base64.toLatin1());
    if (decoded.isEmpty())
        return std::nullopt;
    ImageAttachment attachment;
    attachment.id = QUuid(id);
    attachment.mimeType = mimeType;
    attachment.fileName = fileName;
    attachment.sourcePath = sourcePath;
    attachment.width = width;
    attachment.height = height;
    attachment.resized = resized;
    attachment.data = decoded;
    return attachment;
}

} // namespace ocapi
