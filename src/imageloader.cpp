#include "imageloader.h"

#include <QByteArray>
#include <QFileInfo>

#include <mupdf/fitz.h>

namespace {

bool isJpegPath(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");
}

QImage imageFromFzPixmap(fz_context *ctx, fz_pixmap *pixmap)
{
    if (!ctx || !pixmap) {
        return QImage();
    }

    const int width = fz_pixmap_width(ctx, pixmap);
    const int height = fz_pixmap_height(ctx, pixmap);
    const int stride = fz_pixmap_stride(ctx, pixmap);
    const int components = fz_pixmap_components(ctx, pixmap);
    const unsigned char *samples = fz_pixmap_samples(ctx, pixmap);
    if (width <= 0 || height <= 0 || !samples) {
        return QImage();
    }

    if (components == 3) {
        return QImage(samples, width, height, stride, QImage::Format_RGB888).copy();
    }
    if (components == 4) {
        return QImage(samples, width, height, stride, QImage::Format_RGBA8888).copy();
    }
    return QImage(samples, width, height, stride, QImage::Format_RGB888).copy();
}

QImage loadJpegViaMupdf(const QString &filePath)
{
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        return QImage();
    }

    QImage result;
    const QByteArray rawPath = QFile::encodeName(filePath);

    fz_try(ctx) {
        fz_image *image = fz_new_image_from_file(ctx, rawPath.constData());
        fz_pixmap *pixmap = fz_get_pixmap_from_image(ctx, image, nullptr, nullptr, nullptr, nullptr);
        result = imageFromFzPixmap(ctx, pixmap);
        fz_drop_pixmap(ctx, pixmap);
        fz_drop_image(ctx, image);
    }
    fz_catch(ctx) {
        result = QImage();
    }

    fz_drop_context(ctx);
    return result;
}

} // namespace

QImage loadImageFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return QImage();
    }
    if (isJpegPath(filePath)) {
        QImage image = loadJpegViaMupdf(filePath);
        if (!image.isNull()) {
            return image;
        }
    }
    return QImage(filePath);
}
