#include "ahdpreviewwidget.h"

#include <QPainter>
#include <QPaintEvent>

namespace {

#ifdef CAR_DESK_USE_T507_SDK
void nv21ToRgb888(const uint8_t *nv21, int width, int height, uchar *rgb)
{
    if (!nv21 || width <= 0 || height <= 0 || !rgb) {
        return;
    }

    const int pixelCount = width * height;
    const uint8_t *yPlane = nv21;
    const uint8_t *vuPlane = nv21 + pixelCount;

    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const int yIndex = j * width + i;
            const int uvIndex = (j / 2) * width + (i & ~1);
            const int y = yPlane[yIndex];
            const int v = vuPlane[uvIndex];
            const int u = vuPlane[uvIndex + 1];

            int r = y + ((1436 * (v - 128)) >> 10);
            int g = y - ((352 * (u - 128) + 731 * (v - 128)) >> 10);
            int b = y + ((1814 * (u - 128)) >> 10);

            const int dst = yIndex * 3;
            rgb[dst + 0] = static_cast<uchar>(qBound(0, r, 255));
            rgb[dst + 1] = static_cast<uchar>(qBound(0, g, 255));
            rgb[dst + 2] = static_cast<uchar>(qBound(0, b, 255));
        }
    }
}
#endif

} // namespace

AhdPreviewGLWidget::AhdPreviewGLWidget(AhdCameraPool *pool, QWidget *parent)
    : QWidget(parent)
    , m_pool(pool)
    , m_channelImages(AhdCameraPool::kChannelCount)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
    if (m_pool) {
        connect(m_pool, &AhdCameraPool::framesUpdated, this, [this]() { update(); },
                Qt::QueuedConnection);
    }
}

void AhdPreviewGLWidget::setLayoutSpec(const AhdLayoutSpec &spec)
{
    m_layout = spec;
    update();
}

void AhdPreviewGLWidget::clearChannelCache()
{
    for (ChannelImage &ch : m_channelImages) {
        ch = ChannelImage();
    }
    update();
}

bool AhdPreviewGLWidget::hasDisplayableCache() const
{
    for (const ChannelImage &ch : m_channelImages) {
        if (!ch.image.isNull()) {
            return true;
        }
    }
    return false;
}

QRect AhdPreviewGLWidget::sourceRectFor360Quadrant(const QImage &image, int channelIndex)
{
    if (image.isNull() || channelIndex < 0 || channelIndex >= AhdLayoutSpec::kChannelCount) {
        return QRect();
    }

    const int w = image.width();
    const int h = image.height();
    if (w < 2 || h < 2) {
        return image.rect();
    }

    const int halfW = w / 2;
    const int halfH = h / 2;
    switch (channelIndex) {
    case 0:
        return QRect(0, 0, halfW, halfH);
    case 1:
        return QRect(halfW, 0, w - halfW, halfH);
    case 2:
        return QRect(0, halfH, halfW, h - halfH);
    case 3:
        return QRect(halfW, halfH, w - halfW, h - halfH);
    default:
        return image.rect();
    }
}

bool AhdPreviewGLWidget::ensureChannelImage(int cacheIndex, const AhdCameraPool::FrameSlot &frame)
{
#ifdef CAR_DESK_USE_T507_SDK
    if (cacheIndex < 0 || cacheIndex >= m_channelImages.size() || frame.nv21.isEmpty()
        || frame.width <= 0 || frame.height <= 0 || frame.generation == 0) {
        return false;
    }

    ChannelImage &ch = m_channelImages[cacheIndex];
    if (ch.cachedGeneration == frame.generation && !ch.image.isNull()) {
        return true;
    }

    const size_t bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 3;
    QByteArray rgb(static_cast<int>(bytes), Qt::Uninitialized);
    nv21ToRgb888(reinterpret_cast<const uint8_t *>(frame.nv21.constData()), frame.width,
                 frame.height, reinterpret_cast<uchar *>(rgb.data()));

    ch.image = QImage(reinterpret_cast<const uchar *>(rgb.constData()), frame.width, frame.height,
                      frame.width * 3, QImage::Format_RGB888)
                   .copy();
    ch.width = frame.width;
    ch.height = frame.height;
    ch.cachedGeneration = frame.generation;
    return !ch.image.isNull();
#else
    Q_UNUSED(cacheIndex);
    Q_UNUSED(frame);
    return false;
#endif
}

void AhdPreviewGLWidget::drawViewport(QPainter *painter, const AhdViewport &vp, int channelIndex)
{
    if (!painter || !vp.visible || channelIndex < 0 || channelIndex >= m_channelImages.size()) {
        return;
    }

    const bool compose360 = m_pool && m_pool->uses360Compose();
    const bool quadrantCrop = compose360 && m_pool->uses360QuadrantCrop();
    const int frameIndex = (compose360 && quadrantCrop) ? 0 : channelIndex;
    const int cacheIndex = (compose360 && quadrantCrop) ? 0 : channelIndex;

    const ChannelImage &cached = m_channelImages.at(cacheIndex);
    AhdCameraPool::FrameSlot frame;
    const bool hasFrame = m_pool && m_pool->copyLatestFrame(frameIndex, &frame)
                          && ensureChannelImage(cacheIndex, frame);
    if (!hasFrame) {
        if (cached.image.isNull()) {
            return;
        }
    }

    const ChannelImage &ch = m_channelImages.at(cacheIndex);
    const QRect dest(vp.norm.left() * width(), vp.norm.top() * height(),
                     vp.norm.width() * width(), vp.norm.height() * height());
    if (quadrantCrop) {
        const QRect src = sourceRectFor360Quadrant(ch.image, channelIndex);
        if (src.isEmpty()) {
            return;
        }
        painter->drawImage(dest, ch.image, src);
    } else {
        painter->drawImage(dest, ch.image);
    }
}

void AhdPreviewGLWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    if (!hasDisplayableCache()) {
        painter.fillRect(rect(), Qt::black);
    }

    AhdViewport viewports[AhdLayoutSpec::kChannelCount];
    m_layout.viewports(viewports);
    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        drawViewport(&painter, viewports[i], viewports[i].channel);
    }
}
