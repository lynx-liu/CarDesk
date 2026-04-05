#include "drivingimagewindow.h"
#include "devicedetect.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFrame>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

DrivingImageWindow::DrivingImageWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_previewWrap(nullptr)
    , m_exitHintLabel(nullptr)
    , m_ahdManager(new AhdManager(360, this))
    , m_singleClickTimer(new QTimer(this))
    , m_returning(false)
    , m_previewLoading(true)
    , m_exitInProgress(false)
    , m_startScheduled(false)
    , m_isFullscreen(false)
    , m_fullscreenCameraId(-1)
    , m_pendingClickGlobalPos()
    , m_lastClickMs(0)
    , m_lastClickPos()
{
    setWindowTitle(QStringLiteral("驾驶影像 / Driving"));
    setObjectName("drivingImageWindow");
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAutoFillBackground(true);
    setWindowOpacity(1.0);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        // 车机端始终置顶，避免主界面图标在切换过程中闪到前面。
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
        if (QApplication::primaryScreen()) {
            setGeometry(QApplication::primaryScreen()->geometry());
        }
    } else {
        setFixedSize(1280, 720);
        if (QApplication::primaryScreen()) {
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
        }
    }

    setupUI();
    bindAhdSignals();

    m_singleClickTimer->setSingleShot(true);
    connect(m_singleClickTimer, &QTimer::timeout, this, [this]() {
        handleConfirmedSingleClick(m_pendingClickGlobalPos);
    });
}

void DrivingImageWindow::bindAhdSignals()
{
    connect(m_ahdManager, &AhdManager::previewStarted, this, [this]() {
        if (!m_exitInProgress && isVisible()) {
            setLoadingState(false);
        }
    });
    connect(m_ahdManager, &AhdManager::previewStopped, this, [this]() {
        if (!m_exitInProgress && isVisible() && m_previewLoading) {
            m_exitHintLabel->setText(QStringLiteral("加载中..."));
            layoutCenterHint();
            m_exitHintLabel->show();
        }
    });
    connect(m_ahdManager, &AhdManager::cameraError, this, [this](const QString &message) {
        if (m_exitInProgress) {
            return;
        }
        m_previewLoading = false;
        if (m_exitHintLabel) {
            m_exitHintLabel->setText(message);
            layoutCenterHint();
            m_exitHintLabel->show();
        }
    });
}

void DrivingImageWindow::warmupCamera() {}

void DrivingImageWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    returnToMainSafely();
}

void DrivingImageWindow::hideEvent(QHideEvent *event)
{
    m_startScheduled = false;
    m_singleClickTimer->stop();
    m_isFullscreen = false;
    m_fullscreenCameraId = -1;
    m_lastClickMs = 0;
    stopPreview();
    QMainWindow::hideEvent(event);
}

void DrivingImageWindow::returnToMainSafely()
{
    if (m_returning || m_exitInProgress) {
        return;
    }

    m_returning = true;
    m_exitInProgress = true;
    setLoadingState(true);
    stopPreview();
    hide();
    // 用 singleShot(0) 让事件循环处理 hide() 的屏幕刷新，再通知主窗口返回
    // 避免同步 emit 导致主窗口立即重绘时摄像头窗口还在屏幕上
    QTimer::singleShot(0, this, [this]() {
        m_exitInProgress = false;
        m_returning = false;
        emit requestReturnToMain();
    });
}

void DrivingImageWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    event->accept();

    // 手动双击检测：触摸屏上 mouseDoubleClickEvent 不可靠
    // 两次点击间隔 < doubleClickInterval 且位移 < 60px → 视为双击
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QPoint gpos = event->globalPos();
    const int dx = gpos.x() - m_lastClickPos.x();
    const int dy = gpos.y() - m_lastClickPos.y();
    const bool closeEnough = (dx * dx + dy * dy) < (60 * 60);
    const int dblInterval = qMax(QApplication::doubleClickInterval(), 400);
    if ((nowMs - m_lastClickMs) < dblInterval && closeEnough) {
        // 双击：取消已排队的单击动作，直接退出
        m_singleClickTimer->stop();
        m_lastClickMs = 0; // 重置，避免三击误触发
        if (!m_exitInProgress) {
            returnToMainSafely();
        }
        return;
    }

    m_lastClickMs = nowMs;
    m_lastClickPos = gpos;
    m_pendingClickGlobalPos = gpos;
    m_singleClickTimer->start(dblInterval);
}

void DrivingImageWindow::handleConfirmedSingleClick(const QPoint &globalPos)
{
    if (m_exitInProgress || !isVisible() || !m_previewWrap) {
        return;
    }

    if (m_isFullscreen) {
        m_isFullscreen = false;
        m_fullscreenCameraId = -1;
    } else {
        const QPoint localPos = m_previewWrap->mapFromGlobal(globalPos);
        const int w = m_previewWrap->width();
        const int h = m_previewWrap->height();
        if (localPos.x() < 0 || localPos.y() < 0 || localPos.x() > w || localPos.y() > h) {
            return;
        }

        const bool isLeft = localPos.x() < w / 2;
        const bool isTop = localPos.y() < h / 2;
        if (isLeft && isTop) {
            m_fullscreenCameraId = 0;
        } else if (!isLeft && isTop) {
            m_fullscreenCameraId = 1;
        } else if (isLeft && !isTop) {
            m_fullscreenCameraId = 2;
        } else {
            m_fullscreenCameraId = 3;
        }
        m_isFullscreen = true;
    }

    if (!m_startScheduled) {
        m_startScheduled = true;
        QTimer::singleShot(0, this, [this]() {
            m_startScheduled = false;
            startPreviewIfNeeded();
        });
    }
}

void DrivingImageWindow::setupUI()
{
    auto *central = new QWidget(this);
    central->setStyleSheet("background:#000000;");
    central->setAttribute(Qt::WA_OpaquePaintEvent, true);
    central->setAutoFillBackground(true);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *previewWrap = new QFrame(central);
    m_previewWrap = previewWrap;
    previewWrap->setStyleSheet("QFrame{background:#000000;border:none;}");

    auto *previewLayout = new QVBoxLayout(previewWrap);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    auto *previewSurface = new QFrame(previewWrap);
    previewSurface->setStyleSheet("QFrame{background:#000000;border:none;}");

    m_exitHintLabel = new QLabel(QStringLiteral("加载中..."), previewWrap);
    m_exitHintLabel->setAlignment(Qt::AlignCenter);
    m_exitHintLabel->setStyleSheet("QLabel{background:rgba(0,0,0,0.45);color:#ffffff;border:1px solid #00A9FF;border-radius:10px;padding:8px 14px;font-size:28px;font-weight:700;}");
    m_exitHintLabel->raise();

    previewLayout->addWidget(previewSurface, 1);
    root->addWidget(previewWrap, 1);
    layoutCenterHint();
}

void DrivingImageWindow::layoutCenterHint()
{
    if (!m_previewWrap || !m_exitHintLabel) {
        return;
    }

    if (m_previewWrap->width() < 120 || m_previewWrap->height() < 80) {
        QTimer::singleShot(0, this, [this]() { layoutCenterHint(); });
        return;
    }

    const QFontMetrics fm(m_exitHintLabel->font());
    const int textW = fm.horizontalAdvance(m_exitHintLabel->text());
    const int w = qMin(420, qMax(220, textW + 48));
    const int h = qMax(68, fm.height() + 24);
    const int x = (m_previewWrap->width() - w) / 2;
    const int y = (m_previewWrap->height() - h) / 2;
    m_exitHintLabel->setGeometry(x, y, w, h);
    m_exitHintLabel->raise();
}

void DrivingImageWindow::startPreviewIfNeeded()
{
    if (m_exitInProgress || !isVisible()) {
        return;
    }

    // 先 kill 旧进程，确保标签不会叠加在仍在输出的视频上
    m_ahdManager->stopPreview();
    m_ahdManager->stopCamera();
    m_ahdManager->setCameraId(360);
    m_ahdManager->setPreviewCameraIndex(m_isFullscreen ? m_fullscreenCameraId : -1);

    m_previewLoading = true;

    const QRect rect = previewRectOnScreen();
    if (rect.width() <= 0 || rect.height() <= 0) {
        m_exitHintLabel->setText(QStringLiteral("预览区域无效"));
        layoutCenterHint();
        m_exitHintLabel->show();
        return;
    }

    m_exitHintLabel->setText(m_isFullscreen
                                 ? QStringLiteral("加载中... 单路%1").arg(m_fullscreenCameraId + 1)
                                 : QStringLiteral("加载中... 四路"));
    layoutCenterHint();
    m_exitHintLabel->show();

    if (!m_ahdManager->startCamera()) {
        m_exitHintLabel->setText(QStringLiteral("摄像头启动失败"));
        layoutCenterHint();
        return;
    }
    if (!m_ahdManager->startPreview(rect.x(), rect.y(), rect.width(), rect.height())) {
        m_exitHintLabel->setText(QStringLiteral("预览启动失败"));
        layoutCenterHint();
    }
}

void DrivingImageWindow::stopPreview()
{
    if (m_ahdManager) {
        m_ahdManager->stopPreview();
        m_ahdManager->stopCamera();
    }
}

QRect DrivingImageWindow::previewRectOnScreen() const
{
    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT && QApplication::primaryScreen()) {
        return QApplication::primaryScreen()->geometry();
    }

    QWidget *target = m_previewWrap ? static_cast<QWidget *>(m_previewWrap)
                                    : const_cast<DrivingImageWindow *>(this);
    if (!target) {
        return QRect();
    }

    const QPoint inWindowPos = target->mapTo(const_cast<DrivingImageWindow *>(this), QPoint(0, 0));
    const QPoint winGlobal = const_cast<DrivingImageWindow *>(this)->mapToGlobal(QPoint(0, 0));
    return QRect(winGlobal.x() + inWindowPos.x(),
                 winGlobal.y() + inWindowPos.y(),
                 target->width(),
                 target->height());
}

void DrivingImageWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_HomePage:
    case Qt::Key_Back:
    case Qt::Key_Escape:
        returnToMainSafely();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void DrivingImageWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        if (QApplication::primaryScreen()) {
            setGeometry(QApplication::primaryScreen()->geometry());
        }
        if (!isFullScreen()) {
            showFullScreen();
        }
        setWindowState(windowState() | Qt::WindowFullScreen | Qt::WindowActive);
    }

    raise();
    activateWindow();

    layoutCenterHint();
    QTimer::singleShot(0, this, [this]() { layoutCenterHint(); });
    m_returning = false;
    m_exitInProgress = false;
    m_isFullscreen = false;
    m_fullscreenCameraId = -1;
    setLoadingState(true);
    if (!m_startScheduled) {
        m_startScheduled = true;
        QTimer::singleShot(0, this, [this]() {
            m_startScheduled = false;
            startPreviewIfNeeded();
        });
    }
}

void DrivingImageWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    layoutCenterHint();
}

void DrivingImageWindow::setLoadingState(bool loading)
{
    m_previewLoading = loading;
    if (!m_exitHintLabel) {
        return;
    }

    if (loading) {
        m_exitHintLabel->setText(QStringLiteral("加载中..."));
        layoutCenterHint();
        m_exitHintLabel->show();
        return;
    }

    m_exitHintLabel->hide();
}
