#ifndef AHDPREVIEWWIDGET_H
#define AHDPREVIEWWIDGET_H

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QRectF>
#include <QVector>

#include <array>

#include "ahdcamerapool.h"
#include "ahdlayout.h"

class QLabel;

// OpenGL 着色器直接采样 NV21（Y + VU 双纹理），避免 CPU 转 RGB
class AhdPreviewGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit AhdPreviewGLWidget(AhdCameraPool *pool, QWidget *parent = nullptr);
    ~AhdPreviewGLWidget() override;

    void setLayoutSpec(const AhdLayoutSpec &spec);
    void setShowRecordingBadge(bool show);
    void clearChannelCache();
    bool hasDisplayableCache() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct ChannelTex {
        GLuint yTex = 0;
        GLuint uvTex = 0;
        int width = 0;
        int height = 0;
        quint64 cachedGeneration = 0;
    };

    void releaseGlResources();
    bool ensureChannelTextures(int cacheIndex, const AhdCameraPool::FrameSlot &frame);
    void uploadNv21Textures(ChannelTex *ch, const uint8_t *nv21, int width, int height);
    void drawYuvViewport(const AhdViewport &vp, int channelIndex);
    void updateChannelLabelLayout();
    static QRectF normalized360Quadrant(int channelIndex);

    AhdCameraPool *m_pool;
    AhdLayoutSpec m_layout;
    QVector<ChannelTex> m_channelTex;
    QOpenGLShaderProgram *m_program = nullptr;
    bool m_showRecordingBadge = false;
    bool m_glReady = false;
    std::array<QLabel *, AhdLayoutSpec::kChannelCount> m_channelLabels = {};
    QLabel *m_recordingBadge = nullptr;
};

#endif // AHDPREVIEWWIDGET_H
