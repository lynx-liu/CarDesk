#include "ahdpreviewwidget.h"

#include <QOpenGLContext>
#include <QLabel>
#include <QPixmap>

namespace {

const char *kVertexShader = R"(
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char *kFragmentShader = R"(
precision mediump float;
varying vec2 vTex;
uniform sampler2D uTexY;
uniform sampler2D uTexUV;
void main() {
    float yVal = texture2D(uTexY, vTex).r * 255.0;
    vec4 uvPx = texture2D(uTexUV, vTex);
    float v = uvPx.r * 255.0 - 128.0;
    float u = uvPx.a * 255.0 - 128.0;
    float r = (yVal + 1.40234375 * v) / 255.0;
    float g = (yVal - 0.34375 * u - 0.7138671875 * v) / 255.0;
    float b = (yVal + 1.7734375 * u) / 255.0;
    gl_FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

void setTextureParams()
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace

AhdPreviewGLWidget::AhdPreviewGLWidget(AhdCameraPool *pool, QWidget *parent)
    : QOpenGLWidget(parent)
    , m_pool(pool)
    , m_channelTex(AhdCameraPool::kChannelCount)
{
    QSurfaceFormat fmt = format();
#ifdef CAR_DESK_USE_T507_SDK
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(2, 0);
#endif
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);

    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);

    static const QString kChannelLabelStyle(
        QStringLiteral("QLabel{background-color:rgba(0,0,0,128);color:#FFFFFF;border:none;padding:0px;}"));

    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        auto *label = new QLabel(this);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(kChannelLabelStyle);
        label->hide();
        m_channelLabels[static_cast<size_t>(i)] = label;
    }

    m_recordingBadge = new QLabel(this);
    m_recordingBadge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_recordingBadge->setScaledContents(true);
    m_recordingBadge->hide();

    if (m_pool) {
        connect(m_pool, &AhdCameraPool::framesUpdated, this, [this]() { update(); },
                Qt::QueuedConnection);
    }
}

AhdPreviewGLWidget::~AhdPreviewGLWidget()
{
    makeCurrent();
    releaseGlResources();
    doneCurrent();
}

void AhdPreviewGLWidget::releaseGlResources()
{
    if (!context()) {
        return;
    }
    initializeOpenGLFunctions();

    for (ChannelTex &ch : m_channelTex) {
        if (ch.yTex) {
            glDeleteTextures(1, &ch.yTex);
            ch.yTex = 0;
        }
        if (ch.uvTex) {
            glDeleteTextures(1, &ch.uvTex);
            ch.uvTex = 0;
        }
        ch = ChannelTex();
    }

    delete m_program;
    m_program = nullptr;
    m_glReady = false;
}

void AhdPreviewGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.f, 0.f, 0.f, 1.f);

    m_program = new QOpenGLShaderProgram(this);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !m_program->link()) {
        qWarning() << "[AhdPreviewGL] shader link failed:" << m_program->log();
        delete m_program;
        m_program = nullptr;
        return;
    }
    qDebug() << "[AhdPreviewGL] shader program ready";

    for (ChannelTex &ch : m_channelTex) {
        glGenTextures(1, &ch.yTex);
        glGenTextures(1, &ch.uvTex);
    }

    m_glReady = true;
}

void AhdPreviewGLWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
    updateChannelLabelLayout();
}

void AhdPreviewGLWidget::setLayoutSpec(const AhdLayoutSpec &spec)
{
    m_layout = spec;
    updateChannelLabelLayout();
    update();
}

void AhdPreviewGLWidget::setShowRecordingBadge(bool show)
{
    if (m_showRecordingBadge == show) {
        return;
    }
    m_showRecordingBadge = show;
    updateChannelLabelLayout();
    update();
}

void AhdPreviewGLWidget::clearChannelCache()
{
    makeCurrent();
    for (ChannelTex &ch : m_channelTex) {
        ch.width = 0;
        ch.height = 0;
        ch.cachedGeneration = 0;
    }
    doneCurrent();
    update();
}

bool AhdPreviewGLWidget::hasDisplayableCache() const
{
    for (const ChannelTex &ch : m_channelTex) {
        if (ch.cachedGeneration != 0 && ch.yTex && ch.uvTex) {
            return true;
        }
    }
    return false;
}

QRectF AhdPreviewGLWidget::normalized360Quadrant(int channelIndex)
{
    switch (channelIndex) {
    case 0:
        return QRectF(0.0, 0.0, 0.5, 0.5);
    case 1:
        return QRectF(0.5, 0.0, 0.5, 0.5);
    case 2:
        return QRectF(0.0, 0.5, 0.5, 0.5);
    case 3:
        return QRectF(0.5, 0.5, 0.5, 0.5);
    default:
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }
}

void AhdPreviewGLWidget::uploadNv21Textures(ChannelTex *ch, const uint8_t *nv21, int width,
                                            int height)
{
    if (!ch || !nv21 || width <= 0 || height <= 0) {
        return;
    }

    const int yBytes = width * height;
    const uint8_t *yPlane = nv21;
    const uint8_t *vuPlane = nv21 + yBytes;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ch->yTex);
    if (ch->width == width && ch->height == height) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                        yPlane);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE,
                    GL_UNSIGNED_BYTE, yPlane);
        setTextureParams();
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ch->uvTex);
    const int uvW = width / 2;
    const int uvH = height / 2;
    if (ch->width == width && ch->height == height) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uvW, uvH, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                        vuPlane);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, uvW, uvH, 0, GL_LUMINANCE_ALPHA,
                    GL_UNSIGNED_BYTE, vuPlane);
        setTextureParams();
    }

    ch->width = width;
    ch->height = height;
}

bool AhdPreviewGLWidget::ensureChannelTextures(int cacheIndex,
                                               const AhdCameraPool::FrameSlot &frame)
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_glReady || !m_program || cacheIndex < 0 || cacheIndex >= m_channelTex.size()
        || frame.nv21.isEmpty() || frame.width <= 0 || frame.height <= 0
        || frame.generation == 0) {
        return false;
    }

    ChannelTex &ch = m_channelTex[cacheIndex];
    if (ch.cachedGeneration == frame.generation && ch.yTex && ch.uvTex) {
        return true;
    }

    uploadNv21Textures(&ch, reinterpret_cast<const uint8_t *>(frame.nv21.constData()),
                       frame.width, frame.height);
    ch.cachedGeneration = frame.generation;
    return ch.yTex && ch.uvTex;
#else
    Q_UNUSED(cacheIndex);
    Q_UNUSED(frame);
    return false;
#endif
}

void AhdPreviewGLWidget::drawYuvViewport(const AhdViewport &vp, int channelIndex)
{
    if (!m_program || !vp.visible || channelIndex < 0 || channelIndex >= m_channelTex.size()) {
        return;
    }

    const bool compose360 = m_pool && m_pool->uses360Compose();
    const bool quadrantCrop = compose360 && m_pool->uses360QuadrantCrop();
    const int frameIndex = (compose360 && quadrantCrop) ? 0 : channelIndex;
    const int cacheIndex = (compose360 && quadrantCrop) ? 0 : channelIndex;

    AhdCameraPool::FrameSlot frame;
    const bool hasFrame = m_pool && m_pool->copyLatestFrame(frameIndex, &frame)
                          && ensureChannelTextures(cacheIndex, frame);
    if (!hasFrame) {
        const ChannelTex &cached = m_channelTex.at(cacheIndex);
        if (cached.cachedGeneration == 0) {
            return;
        }
    }

    const ChannelTex &ch = m_channelTex.at(cacheIndex);
    if (!ch.yTex || !ch.uvTex) {
        return;
    }

    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    if (w < 1.f || h < 1.f) {
        return;
    }

    const float x0 = vp.norm.left() * w;
    const float y0 = vp.norm.top() * h;
    const float x1 = (vp.norm.left() + vp.norm.width()) * w;
    const float y1 = (vp.norm.top() + vp.norm.height()) * h;

    const float ndcL = (x0 / w) * 2.f - 1.f;
    const float ndcR = (x1 / w) * 2.f - 1.f;
    const float ndcT = 1.f - (y0 / h) * 2.f;
    const float ndcB = 1.f - (y1 / h) * 2.f;

    QRectF src(0.0, 0.0, 1.0, 1.0);
    if (quadrantCrop) {
        src = normalized360Quadrant(channelIndex);
    }

    const float u0 = static_cast<float>(src.left());
    const float v0 = static_cast<float>(src.top());
    const float u1 = static_cast<float>(src.left() + src.width());
    const float v1 = static_cast<float>(src.top() + src.height());

    const float verts[] = {
        ndcL, ndcB, u0, v1, ndcR, ndcB, u1, v1, ndcL, ndcT, u0, v0,
        ndcR, ndcT, u1, v0,
    };

    m_program->bind();
    m_program->setUniformValue("uTexY", 0);
    m_program->setUniformValue("uTexUV", 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ch.yTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ch.uvTex);

    const int posLoc = m_program->attributeLocation("aPos");
    const int texLoc = m_program->attributeLocation("aTex");
    m_program->enableAttributeArray(posLoc);
    m_program->enableAttributeArray(texLoc);
    m_program->setAttributeArray(posLoc, GL_FLOAT, verts, 2, 4 * sizeof(float));
    m_program->setAttributeArray(texLoc, GL_FLOAT, verts + 2, 2, 4 * sizeof(float));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(posLoc);
    m_program->disableAttributeArray(texLoc);
    m_program->release();
}

void AhdPreviewGLWidget::updateChannelLabelLayout()
{
    if (width() < 8 || height() < 8) {
        return;
    }

    AhdViewport viewports[AhdLayoutSpec::kChannelCount];
    m_layout.viewports(viewports);

    QFont font;
    font.setPixelSize(qMax(22, height() * 30 / 720));
    font.setBold(true);

    const int badge = qMax(36, height() * 48 / 720);
    bool showRec = false;
    QRect recRect;

    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        QLabel *label = m_channelLabels[static_cast<size_t>(i)];
        const AhdViewport &vp = viewports[i];
        if (!label || !vp.visible || vp.channel < 0) {
            if (label) {
                label->hide();
            }
            continue;
        }

        const QString text = AhdLayoutSpec::channelLabel(vp.channel);
        if (text.isEmpty()) {
            label->hide();
            continue;
        }

        const QRect dest(static_cast<int>(vp.norm.left() * width()),
                         static_cast<int>(vp.norm.top() * height()),
                         static_cast<int>(vp.norm.width() * width()),
                         static_cast<int>(vp.norm.height() * height()));
        if (dest.width() < 8 || dest.height() < 8) {
            label->hide();
            continue;
        }

        const QRect badgeRect(dest.right() - badge + 1, dest.top(), badge, badge);
        label->setFont(font);
        label->setText(text);
        label->setGeometry(badgeRect);
        label->show();
        label->raise();

        if (m_showRecordingBadge && AhdLayoutSpec::isRearChannel(vp.channel)) {
            const int iconSide = qMax(24, badge * 2 / 3);
            recRect = QRect(badgeRect.left() - iconSide - 6, badgeRect.top() + 6, iconSide, iconSide);
            showRec = true;
        }
    }

    if (m_recordingBadge) {
        if (showRec) {
            static const QPixmap recIcon(
                QStringLiteral(":/images/pict_driving_image_video_recording.png"));
            if (!recIcon.isNull()) {
                m_recordingBadge->setPixmap(
                    recIcon.scaled(recRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_recordingBadge->setGeometry(recRect);
                m_recordingBadge->show();
                m_recordingBadge->raise();
            } else {
                m_recordingBadge->hide();
            }
        } else {
            m_recordingBadge->hide();
        }
    }
}

void AhdPreviewGLWidget::paintGL()
{
    if (!m_glReady || !m_program) {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        for (QLabel *label : m_channelLabels) {
            if (label) {
                label->hide();
            }
        }
        if (m_recordingBadge) {
            m_recordingBadge->hide();
        }
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    AhdViewport viewports[AhdLayoutSpec::kChannelCount];
    m_layout.viewports(viewports);
    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        drawYuvViewport(viewports[i], viewports[i].channel);
    }
}
