#ifndef AHDPREVIEWWIDGET_H
#define AHDPREVIEWWIDGET_H

#include <QImage>
#include <QVector>
#include <QWidget>

#include "ahdcamerapool.h"
#include "ahdlayout.h"

// CPU 绘制预览，避免与 SDK（libsdk_camera 内 EGL/Mali）争用同一显示栈
class AhdPreviewGLWidget : public QWidget {
    Q_OBJECT

public:
    explicit AhdPreviewGLWidget(AhdCameraPool *pool, QWidget *parent = nullptr);

    void setLayoutSpec(const AhdLayoutSpec &spec);
    void setShowRecordingBadge(bool show);
    void clearChannelCache();
    bool hasDisplayableCache() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct ChannelImage {
        int width = 0;
        int height = 0;
        quint64 cachedGeneration = 0;
        QImage image;
    };

    bool ensureChannelImage(int cacheIndex, const AhdCameraPool::FrameSlot &frame);
    void drawViewport(QPainter *painter, const AhdViewport &vp, int channelIndex);
    void drawChannelOverlays(QPainter *painter);
    static QRect sourceRectFor360Quadrant(const QImage &image, int channelIndex);

    AhdCameraPool *m_pool;
    AhdLayoutSpec m_layout;
    QVector<ChannelImage> m_channelImages;
    bool m_showRecordingBadge = false;
};

#endif // AHDPREVIEWWIDGET_H
