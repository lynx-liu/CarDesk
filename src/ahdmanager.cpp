#include "ahdmanager.h"

#include "ahdcamerapool.h"
#include "ahdpreviewoverlaywidget.h"
#include "ahdpreviewwidget.h"
#include "ahdsettings.h"
#include "appsettings.h"

#include <QProcessEnvironment>
#include <QRect>
#include <QTimer>
#include <QWidget>

AhdManager::AhdManager(int layoutMode, QObject *parent)
    : QObject(parent)
    , m_pool(new AhdCameraPool(this))
    , m_previewWidget(nullptr)
    , m_camReady(false)
    , m_prevActive(false)
    , m_previewCameraIndex(-1)
{
    m_layout.mode = layoutMode;
    connect(m_pool, &AhdCameraPool::poolError, this, &AhdManager::cameraError);
    connect(m_pool, &AhdCameraPool::recordingActiveChanged, this, [this](bool) {
        updateRecordingBadge();
        updateCameraFaultOverlay();
    });
    connect(m_pool, &AhdCameraPool::cameraFaultsChanged, this, &AhdManager::updateCameraFaultOverlay);
    if (AppSettings::debugMode()) {
        connect(m_pool, &AhdCameraPool::previewFpsChanged, this, &AhdManager::updateFpsOverlay);
    }
}

AhdManager::~AhdManager()
{
    stopCamera();
}

void AhdManager::globalInit()
{
    AhdCameraPool::globalInit();
}

void AhdManager::globalCleanup()
{
    AhdCameraPool::globalCleanup();
}

void AhdManager::setLayoutMode(int mode)
{
    if (m_layout.mode == mode && m_previewCameraIndex < 0) {
        return;
    }
    m_layout.mode = mode;
    m_previewCameraIndex = -1;
    applyLayoutSpec();
}

int AhdManager::layoutMode() const
{
    return m_layout.mode;
}

void AhdManager::setPreviewCameraIndex(int previewCameraIndex)
{
    if (previewCameraIndex < -1) {
        previewCameraIndex = -1;
    }
    if (previewCameraIndex >= AhdLayoutSpec::kChannelCount) {
        previewCameraIndex = AhdLayoutSpec::kChannelCount - 1;
    }
    if (m_previewCameraIndex == previewCameraIndex) {
        return;
    }
    m_previewCameraIndex = previewCameraIndex;
    applyLayoutSpec();
}

void AhdManager::applyLayoutSpec()
{
    m_layout.fullscreenChannel = m_previewCameraIndex;
    if (m_pool) {
        m_pool->setLayoutSpec(m_layout);
    }
    if (m_previewWidget) {
        m_previewWidget->setLayoutSpec(m_layout);
        m_previewWidget->update();
    }
    if (m_overlayWidget) {
        m_overlayWidget->setLayoutSpec(m_layout);
    }
}

bool AhdManager::startCamera()
{
    if (m_camReady && m_pool->isRunning()) {
        return true;
    }

    if (!m_pool->startAll()) {
        return false;
    }

    m_camReady = true;
    applyLayoutSpec();
    return true;
}

bool AhdManager::warmupHardware()
{
    if (m_camReady && m_pool->isRunning()) {
        return true;
    }

    if (!m_pool->startAll(true)) {
        return false;
    }

    m_camReady = true;
    applyLayoutSpec();
    return true;
}

void AhdManager::stopCamera()
{
    if (m_pool && (m_pool->isRunning() || m_camReady)) {
        m_pool->stopAll();
    }
    m_camReady = false;
    m_prevActive = false;
}

bool AhdManager::startPreview(QWidget *parentWidget, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        emit cameraError(QStringLiteral("startPreview 参数无效 (mode=%1, rect=%2,%3 %4x%5)")
                             .arg(m_layout.mode)
                             .arg(x)
                             .arg(y)
                             .arg(w)
                             .arg(h));
        return false;
    }

    const QRect rect(x < 0 ? 0 : x, y < 0 ? 0 : y, w, h);
    const bool rectChanged = (m_lastRect != rect);
    m_lastRect = rect;

    // 先创建 GL 预览控件再打开 SDK，避免首帧回调时无有效绘制上下文导致堆损坏
    attachPreviewWidget(parentWidget, rect.width(), rect.height());

    if (!m_camReady && !startCamera()) {
        return false;
    }
    if (m_previewWidget) {
        if (!m_pool->hasPersistedFactories()) {
            m_previewWidget->clearChannelCache();
        }
        // 预览先出图，录像状态稍后同步（无 TF 时仅跳过 startRecord，不阻塞预览）
        QTimer::singleShot(0, this, &AhdManager::flushRecordingSync);
    }

    if (!m_prevActive) {
        m_prevActive = true;
        emit previewStarted();
    } else if (rectChanged && m_previewWidget) {
        m_previewWidget->update();
    }

    applyLayoutSpec();
    if (AppSettings::debugMode()) {
        updateFpsOverlay();
    }
    return true;
}

void AhdManager::attachPreviewWidget(QWidget *parentWidget, int w, int h)
{
    if (!m_previewWidget) {
        m_previewWidget = new AhdPreviewGLWidget(m_pool, parentWidget);
        m_previewWidget->setLayoutSpec(m_layout);
    } else if (parentWidget && m_previewWidget->parentWidget() != parentWidget) {
        m_previewWidget->setParent(parentWidget);
    }

    if (!m_overlayWidget) {
        m_overlayWidget = new AhdPreviewOverlayWidget(parentWidget);
        m_overlayWidget->setLayoutSpec(m_layout);
        m_overlayWidget->setShowChannelFps(AppSettings::debugMode());
    } else if (parentWidget && m_overlayWidget->parentWidget() != parentWidget) {
        m_overlayWidget->setParent(parentWidget);
        m_overlayWidget->setLayoutSpec(m_layout);
        m_overlayWidget->setShowChannelFps(AppSettings::debugMode());
    }

    const int pw = (w > 0) ? w : (parentWidget ? parentWidget->width() : 0);
    const int ph = (h > 0) ? h : (parentWidget ? parentWidget->height() : 0);
    const QRect geom(0, 0, pw, ph);
    m_previewWidget->setGeometry(geom);
    m_previewWidget->show();
    m_previewWidget->lower();

    m_overlayWidget->setGeometry(geom);
    m_overlayWidget->show();
    m_overlayWidget->raise();
}

void AhdManager::stopPreview()
{
    if (!m_prevActive) {
        return;
    }
    if (m_previewWidget) {
        m_previewWidget->hide();
    }
    if (m_overlayWidget) {
        m_overlayWidget->hide();
    }
    m_prevActive = false;
    emit previewStopped();
}

bool AhdManager::isCameraReady() const
{
    return m_camReady;
}

bool AhdManager::isPreviewActive() const
{
    return m_prevActive;
}

bool AhdManager::hasWarmCameraPool() const
{
    return m_pool && m_pool->hasPersistedFactories();
}

AhdPreviewGLWidget *AhdManager::previewWidget() const
{
    return m_previewWidget;
}

void AhdManager::enableSafetyWatermark(const QString &text)
{
    if (m_pool) {
        m_pool->applySafetyWatermarks(text);
    }
}

void AhdManager::clearWatermark() {}

void AhdManager::syncRecordingWithSettings()
{
    if (!m_recordSyncTimer) {
        m_recordSyncTimer = new QTimer(this);
        m_recordSyncTimer->setSingleShot(true);
        m_recordSyncTimer->setInterval(1500);
        connect(m_recordSyncTimer, &QTimer::timeout, this, &AhdManager::flushRecordingSync);
    }
    m_recordSyncTimer->start();
}

void AhdManager::syncRecordingWithSettingsNow()
{
    if (m_recordSyncTimer && m_recordSyncTimer->isActive()) {
        m_recordSyncTimer->stop();
    }
    flushRecordingSync();
}

void AhdManager::flushRecordingSync()
{
    if (m_pool) {
        m_pool->scheduleRecordingSync();
    }
    updateRecordingBadge();
    updateCameraFaultOverlay();
}

void AhdManager::updateRecordingBadge()
{
    if (!m_overlayWidget) {
        return;
    }
    const bool active = m_pool && m_pool->isRecordingActive();
    m_overlayWidget->setShowRecordingBadge(active);
}

void AhdManager::updateCameraFaultOverlay()
{
    if (!m_overlayWidget || !m_pool) {
        return;
    }

    QString texts[AhdLayoutSpec::kChannelCount];
    if (m_pool->isRecordingActive()) {
        for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
            texts[i] = m_pool->cameraFaultText(i);
        }
    }
    m_overlayWidget->setChannelFaultTexts(texts);
}

void AhdManager::updateFpsOverlay()
{
    if (!m_overlayWidget || !m_pool || !AppSettings::debugMode()) {
        return;
    }

    double fps[AhdLayoutSpec::kChannelCount];
    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        fps[i] = m_pool->channelPreviewFps(i);
    }
    m_overlayWidget->setChannelFpsValues(fps);
}
