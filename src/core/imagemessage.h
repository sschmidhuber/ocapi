#pragma once

#include <QByteArray>
#include <QImage>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <optional>

namespace ocapi {

struct ImageAttachment {
    QUuid id = QUuid::createUuid();
    QString mimeType;
    QString fileName;
    QString sourcePath;
    int width = 0;
    int height = 0;
    bool resized = false;
    QByteArray data; // raw encoded image bytes

    [[nodiscard]] QString base64Data() const { return QString::fromLatin1(data.toBase64()); }
    [[nodiscard]] QImage decode() const { return QImage::fromData(data); }
};

class ImagePreprocessor {
public:
    // JPEG, PNG, WebP, JPEG XL, AVIF, TIFF and GIF are accepted as input.
    [[nodiscard]] static QStringList supportedMimeTypes();
    [[nodiscard]] static QString supportedNameFilter();

    // Loads, validates and preprocesses an image file. Returns nullopt and
    // fills \p error when the file cannot be read or is not a supported image.
    [[nodiscard]] static std::optional<ImageAttachment> processFile(const QString &path,
                                                                    QString *error = nullptr);
    // Re-encodes a decoded image (e.g. after resizing or when the source
    // format is not widely supported by model providers).
    [[nodiscard]] static std::optional<ImageAttachment>
    processImage(QImage image, QString mimeType, const QString &fileName, QString *error = nullptr);

    [[nodiscard]] static std::optional<ImageAttachment>
    fromBase64(const QString &id, const QString &mimeType, const QString &fileName, int width,
               int height, bool resized, const QString &sourcePath, const QString &base64);

    [[nodiscard]] static int maxDimension() { return 1536; }

private:
    [[nodiscard]] static std::optional<QString> sniffMimeType(const QByteArray &bytes);
    [[nodiscard]] static QByteArray encode(const QImage &image, QString *mimeType);
};

} // namespace ocapi

Q_DECLARE_METATYPE(ocapi::ImageAttachment)
Q_DECLARE_METATYPE(QVector<ocapi::ImageAttachment>)
