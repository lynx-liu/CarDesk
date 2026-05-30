#include "drivingimagewindow.h"
#include "automotivedriving.h"
#include "ahdpreviewwidget.h"
#include "ahdsettings.h"
#include "pagebgwidget.h"
#include "drivingimagenavbar.h"
#include "drivingimageplaybackpage.h"
#include "drivingimagesettingspage.h"
#include "drivingimagepreviewtopbar.h"
#include "devicedetect.h"
#include "mainwindow.h"
#include "mediamanager.h"

#include <QApplication>
#include <QDebug>
#include <QProcessEnvironment>
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
#include <QPixmap>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QString verticalTipChars(const QString &text)
{
    QString out;
    for (const QChar &ch : text) {
        out.append(ch);
        out.append(QLatin1Char('\n'));
    }
    if (!out.isEmpty()) {
        out.chop(1);
    }
    return out;
}

int verticalTipTextHeight(const QFont &font, int lineCount)
{
    const QFontMetrics fm(font);
    return fm.lineSpacing() * lineCount + fm.descent();
}

} // namespace

DrivingImageWindow::DrivingImageWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_previewWrap(nullptr)
    , m_exitHintLabel(nullptr)
    , m_singleClickTimer(new QTimer(this))
    , m_returning(false)
    , m_exitInProgress(false)
    , m_startScheduled(false)
    , m_isFullscreen(false)
    , m_fullscreenCameraId(-1)
    , m_cameraMode(360)
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

    m_singleClickTimer->setSingleShot(true);
    connect(m_singleClickTimer, &QTimer::timeout, this, [this]() {
        handleConfirmedSingleClick(m_pendingClickGlobalPos);
    });

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(2000);
    connect(m_longPressTimer, &QTimer::timeout, this, &DrivingImageWindow::onLongPressTimeout);

    m_previewChromeHideTimer = new QTimer(this);
    m_previewChromeHideTimer->setSingleShot(true);
    m_previewChromeHideTimer->setInterval(3000);
    connect(m_previewChromeHideTimer, &QTimer::timeout, this,
            &DrivingImageWindow::onPreviewChromeHideTimeout);
}

AhdManager *DrivingImageWindow::ahdManager()
{
    if (!m_ahdManager) {
        m_ahdManager = new AhdManager(360, this);
        bindAhdSignals();
    }
    return m_ahdManager;
}

void DrivingImageWindow::bindAhdSignals()
{
    connect(ahdManager(), &AhdManager::previewStarted, this, [this]() {
        if (!m_exitInProgress && isVisible()) {
            if (m_exitHintLabel) {
                m_exitHintLabel->hide();
            }
            updatePreviewChrome();
        }
    });
    connect(ahdManager(), &AhdManager::cameraError, this, [this](const QString &message) {
        if (m_exitInProgress) {
            return;
        }
        if (m_exitHintLabel) {
            const QString faultText = message.isEmpty()
                                          ? QStringLiteral("影像功能出现故障")
                                          : message;
            m_exitHintLabel->setText(faultText);
            layoutCenterHint();
            m_exitHintLabel->show();
        }
    });
}

void DrivingImageWindow::warmupCamera()
{
    if (qEnvironmentVariableIsSet("CARDESK_SKIP_AHD")) {
        qWarning() << "[Driving] CARDESK_SKIP_AHD=1, skip camera warmup";
        return;
    }

    m_cameraMode = 360;
    updatePreviewLayout();

    AhdManager *mgr = ahdManager();
    if (mgr->isCameraReady()) {
        qDebug() << "[Driving] warmupCamera: already ready";
        return;
    }

    qDebug() << "[Driving] warmupCamera: warmupHardware mode" << m_cameraMode;
    if (!mgr->warmupHardware()) {
        qWarning() << "[Driving] warmupCamera: warmupHardware failed";
        return;
    }
    mgr->enableSafetyWatermark(QStringLiteral("请注意周边安全"));
}

void DrivingImageWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    returnToMainSafely();
}

void DrivingImageWindow::hideEvent(QHideEvent *event)
{
    m_startScheduled = false;
    m_singleClickTimer->stop();
    if (m_longPressTimer) {
        m_longPressTimer->stop();
    }
    cancelPreviewChromeAutoHide();
    m_previewChromeVisible = false;
    m_longPressTriggered = false;
    m_isFullscreen = false;
    m_fullscreenCameraId = -1;
    m_lastClickMs = 0;
    if (m_exitInProgress) {
        QMainWindow::hideEvent(event);
        return;
    }
    if (m_ahdManager) {
        m_ahdManager->stopPreview();
    }
    QMainWindow::hideEvent(event);
}

void DrivingImageWindow::returnToMainSafely()
{
    if (m_returning || m_exitInProgress) {
        return;
    }

    if (!automotiveCanUserCloseDrivingImage()) {
        qDebug() << "[Driving] returnToMain blocked: turn/reverse active";
        return;
    }

    automotiveNotifyUserClosedDrivingImage();

    m_returning = true;
    m_exitInProgress = true;

    if (m_ahdManager) {
        m_ahdManager->stopPreview();
    }
    hide();

    // 与切到「设置/回放」一致：仅隐藏 Qt 预览，摄像头保持运行便于再次进入
    QTimer::singleShot(0, this, [this]() {
        m_exitInProgress = false;
        m_returning = false;
        emit requestReturnToMain();
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *main = qobject_cast<MainWindow *>(widget)) {
                if (main->mediaManager()) {
                    main->mediaManager()->resumePlaybackAfterInterruption();
                }
                break;
            }
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget != this && widget->isVisible()) {
                widget->update();
                widget->repaint();
            }
        }
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        QTimer::singleShot(60, this, [this]() {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (widget != this && widget->isVisible()) {
                    widget->update();
                    widget->repaint();
                }
            }
        });
    });
}

void DrivingImageWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    event->accept();

    if (m_previewChromeVisible) {
        schedulePreviewChromeAutoHide();
    }

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
        if (m_longPressTimer) {
            m_longPressTimer->stop();
        }
        m_longPressTriggered = false;
        m_lastClickMs = 0; // 重置，避免三击误触发
        if (!m_exitInProgress) {
            returnToMainSafely();
        }
        return;
    }

    m_lastClickMs = nowMs;
    m_lastClickPos = gpos;
    m_pendingClickGlobalPos = gpos;

    m_longPressTriggered = false;
    if (canStartPreviewLongPress(gpos) && m_longPressTimer) {
        m_longPressTimer->start();
    }
}

void DrivingImageWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_longPressTimer && m_longPressTimer->isActive()) {
            m_longPressTimer->stop();
        }
        if (!m_longPressTriggered && m_stack && m_stack->currentIndex() == 0 && !m_exitInProgress) {
            m_singleClickTimer->start(qMax(QApplication::doubleClickInterval(), 400));
        }
        m_longPressTriggered = false;
    }
    QMainWindow::mouseReleaseEvent(event);
}

bool DrivingImageWindow::canStartPreviewLongPress(const QPoint &globalPos) const
{
    if (m_exitInProgress || !isVisible()) {
        return false;
    }
    if (!m_stack || m_stack->currentIndex() != 0) {
        return false;
    }
    if (m_cameraMode != 360 || m_previewChromeVisible) {
        return false;
    }
    if (m_navBar && m_navBar->isVisible()) {
        const QPoint navLocal = m_navBar->mapFromGlobal(globalPos);
        if (m_navBar->rect().contains(navLocal)) {
            return false;
        }
    }
    if (m_previewTopBar && m_previewTopBar->isVisible()) {
        const QPoint topLocal = m_previewTopBar->mapFromGlobal(globalPos);
        if (m_previewTopBar->rect().contains(topLocal)) {
            return false;
        }
    }
    return true;
}

void DrivingImageWindow::onLongPressTimeout()
{
    if (m_cameraMode != 360 || m_previewChromeVisible) {
        return;
    }
    if (!m_stack || m_stack->currentIndex() != 0) {
        return;
    }
    m_longPressTriggered = true;
    m_singleClickTimer->stop();
    m_lastClickMs = 0;
    m_previewChromeVisible = true;
    updatePreviewChrome();
    schedulePreviewChromeAutoHide();
}

void DrivingImageWindow::schedulePreviewChromeAutoHide()
{
    if (!m_previewChromeVisible || !m_previewChromeHideTimer) {
        return;
    }
    m_previewChromeHideTimer->start();
}

void DrivingImageWindow::cancelPreviewChromeAutoHide()
{
    if (m_previewChromeHideTimer) {
        m_previewChromeHideTimer->stop();
    }
}

void DrivingImageWindow::hidePreviewChrome()
{
    cancelPreviewChromeAutoHide();
    if (!m_previewChromeVisible) {
        return;
    }
    m_previewChromeVisible = false;
    updatePreviewChrome();
}

void DrivingImageWindow::onPreviewChromeHideTimeout()
{
    hidePreviewChrome();
}

void DrivingImageWindow::handleConfirmedSingleClick(const QPoint &globalPos)
{
    if (m_stack && m_stack->currentIndex() != 0) {
        return;
    }
    if (m_exitInProgress || !isVisible() || !m_previewWrap) {
        return;
    }
    if (m_navBar && m_navBar->isVisible()) {
        const QPoint navLocal = m_navBar->mapFromGlobal(globalPos);
        if (m_navBar->rect().contains(navLocal)) {
            if (m_previewChromeVisible) {
                schedulePreviewChromeAutoHide();
            }
            return;
        }
    }

    if (m_previewChromeVisible) {
        schedulePreviewChromeAutoHide();
    }

    // 仅四分屏(360)可单击切换单摄；不影响 CAN/按键触发的转向、倒车、行车布局切换
    if (m_cameraMode != 360) {
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
    auto *central = new PageBgWidget(this);
    central->setObjectName(QStringLiteral("drivingImageCentral"));
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(central);
    m_stack->setMinimumHeight(0);
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack->setStyleSheet(
        QStringLiteral("QStackedWidget{background:transparent;border:none;}"));
    m_previewPage = createPreviewPage();
    m_stack->addWidget(m_previewPage);
    m_settingsPage = new DrivingImageSettingsPage(ahdManager(), m_stack);
    m_stack->addWidget(m_settingsPage);
    m_playbackPage = new DrivingImagePlaybackPage(m_stack);
    m_stack->addWidget(m_playbackPage);
    root->addWidget(m_stack, 1);

    // 预览全屏 720px，底栏叠在画面上（原型 .video_play_bottom position:absolute）
    m_navBar = new DrivingImageNavBar(central);
    m_navBar->setFixedHeight(108);
    m_navBar->hide();

    m_previewTopBar = new DrivingImagePreviewTopBar(central);
    m_previewTopBar->setTitle(QStringLiteral("行车影像"));
    m_previewTopBar->hide();
    connect(m_previewTopBar, &DrivingImagePreviewTopBar::backClicked, this,
            &DrivingImageWindow::returnToMainSafely);

    connect(m_navBar, &DrivingImageNavBar::tabSelected, this, [this](DrivingImageNavBar::Tab tab) {
        showPage(static_cast<int>(tab));
    });

    connect(m_settingsPage, &DrivingImageSettingsPage::recordingToggled, this, [this](bool) {
        if (m_ahdManager) {
            m_ahdManager->syncRecordingWithSettings();
        }
    });
    connect(m_settingsPage, &DrivingImageSettingsPage::requestReturnToMain, this, [this]() {
        returnToMainSafely();
    });
    connect(m_playbackPage, &DrivingImagePlaybackPage::requestReturnToPreview, this, [this]() {
        showPage(0);
    });
    connect(m_playbackPage, &DrivingImagePlaybackPage::requestReturnToMain, this, [this]() {
        returnToMainSafely();
    });
    connect(m_playbackPage, &DrivingImagePlaybackPage::requestPlayVideo, this,
            &DrivingImageWindow::requestPlayRecordVideo);
    showPage(0);
}

QWidget *DrivingImageWindow::createPreviewPage()
{
    auto *page = new QWidget();
    page->setMinimumSize(0, 0);
    page->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_previewWrap = new QFrame(page);
    m_previewWrap->setStyleSheet(QStringLiteral("QFrame{background:#000000;border:none;}"));
    layout->addWidget(m_previewWrap, 1);

    auto *previewSurface = new QFrame(m_previewWrap);
    previewSurface->setStyleSheet(QStringLiteral("QFrame{background:#000000;border:none;}"));
    previewSurface->setGeometry(m_previewWrap->rect());

    // 对齐 UI .video_play_tips：整条半透明底，图标 + 竖排文字同一容器
    m_safetyTipFrame = new QFrame(m_previewWrap);
    m_safetyTipFrame->setStyleSheet(
        QStringLiteral("QFrame{background:rgba(0,0,0,128);border:none;}"));
    m_safetyTipIcon = new QLabel(m_safetyTipFrame);
    m_safetyTipIcon->setPixmap(QPixmap(QStringLiteral(":/images/pict_driving_image_tips_icon.png")));
    m_safetyTipIcon->setAlignment(Qt::AlignCenter);
    m_safetyTipIcon->setScaledContents(true);
    m_safetyTipIcon->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    m_safetyTipText = new QLabel(verticalTipChars(QStringLiteral("请注意周边安全")), m_safetyTipFrame);
    m_safetyTipText->setAlignment(Qt::AlignCenter);
    m_safetyTipText->setWordWrap(true);
    m_safetyTipText->setStyleSheet(
        QStringLiteral("QLabel{color:#E3D948;background:transparent;border:none;font-size:30px;"
                       "font-weight:700;}"));

    m_exitHintLabel = new QLabel(m_previewWrap);
    m_exitHintLabel->setAlignment(Qt::AlignCenter);
    m_exitHintLabel->setStyleSheet(
        QStringLiteral("QLabel{background:rgba(0,0,0,0.45);color:#ffffff;border:1px solid #00A9FF;"
                       "border-radius:10px;padding:8px 14px;font-size:28px;font-weight:700;}"));
    m_exitHintLabel->hide();

    m_longPressHintLabel = new QLabel(verticalTipChars(QStringLiteral("长按屏幕呼出状态栏")),
                                      m_previewWrap);
    m_longPressHintLabel->setAlignment(Qt::AlignCenter);
    m_longPressHintLabel->setWordWrap(true);
    m_longPressHintLabel->setStyleSheet(
        QStringLiteral("QLabel{color:#E3D948;background:rgba(0,0,0,128);border:none;"
                       "font-size:30px;font-weight:700;padding:0px;}"));
    m_longPressHintLabel->hide();

    return page;
}

void DrivingImageWindow::layoutNavBar()
{
    QWidget *host = centralWidget();
    if (!m_navBar || !host) {
        return;
    }
    const int navH = m_navBar->height() > 0 ? m_navBar->height() : 108;
    const int w = host->width();
    const int h = host->height();
    if (w <= 0 || h <= 0) {
        return;
    }
    m_navBar->setGeometry(0, h - navH, w, navH);
    m_navBar->raise();
}

void DrivingImageWindow::layoutLongPressHint()
{
    if (!m_previewWrap || !m_longPressHintLabel) {
        return;
    }
    const int w = m_previewWrap->width();
    const int h = m_previewWrap->height();
    if (w < 120 || h < 80) {
        return;
    }
    const int tipW = qMax(48, w * 60 / 1280);
    const int tipTop = h * 173 / 720;
    const int tipH = qMax(132, h * 400 / 720);
    m_longPressHintLabel->setGeometry(w - tipW, tipTop, tipW, tipH);
    const int padH = qMax(4, tipW * 8 / 60);
    const int padTop = qMax(18, tipH * 22 / 400);
    const int padBottom = qMax(8, tipH * 10 / 400);
    const int textAvailH = tipH - padTop - padBottom;
    int fontPx = qMax(18, tipW * 30 / 60);
    QFont tipFont;
    tipFont.setBold(true);
    const int textLines = 9;
    int textNeedH = 0;
    do {
        tipFont.setPixelSize(fontPx);
        textNeedH = verticalTipTextHeight(tipFont, textLines);
        if (textNeedH <= textAvailH || fontPx <= 16) {
            break;
        }
        --fontPx;
    } while (true);
    m_longPressHintLabel->setFont(tipFont);
    m_longPressHintLabel->setStyleSheet(
        QStringLiteral("QLabel{color:#E3D948;background:rgba(0,0,0,128);border:none;padding:%1px %2px %3px %2px;}")
            .arg(padTop)
            .arg(padH)
            .arg(padBottom));
    m_longPressHintLabel->raise();
}

void DrivingImageWindow::layoutPreviewTopBar()
{
    QWidget *host = centralWidget();
    if (!m_previewTopBar || !host) {
        return;
    }
    const int w = host->width();
    if (w <= 0) {
        return;
    }
    const int topH = qMax(48, host->height() * 72 / 720);
    m_previewTopBar->setFixedSize(w, topH);
    m_previewTopBar->setGeometry(0, 0, w, topH);
    m_previewTopBar->raise();
}

void DrivingImageWindow::updatePreviewChrome()
{
    const bool onPreview = m_stack && m_stack->currentIndex() == 0;
    const bool quadMode = m_cameraMode == 360;

    if (!onPreview) {
        if (m_longPressHintLabel) {
            m_longPressHintLabel->hide();
        }
        if (m_previewTopBar) {
            m_previewTopBar->hide();
        }
        if (m_navBar) {
            layoutNavBar();
            m_navBar->show();
            m_navBar->raise();
        }
        return;
    }

    if (!quadMode) {
        if (m_longPressHintLabel) {
            m_longPressHintLabel->hide();
        }
        if (m_previewTopBar) {
            m_previewTopBar->hide();
        }
        if (m_navBar) {
            m_navBar->hide();
        }
        return;
    }

    if (m_previewChromeVisible) {
        if (m_longPressHintLabel) {
            m_longPressHintLabel->hide();
        }
        layoutPreviewTopBar();
        if (m_previewTopBar) {
            m_previewTopBar->show();
            m_previewTopBar->raise();
        }
        layoutNavBar();
        if (m_navBar) {
            m_navBar->show();
            m_navBar->raise();
        }
    } else {
        if (m_previewTopBar) {
            m_previewTopBar->hide();
        }
        if (m_navBar) {
            m_navBar->hide();
        }
        layoutLongPressHint();
        if (m_longPressHintLabel) {
            m_longPressHintLabel->show();
            m_longPressHintLabel->raise();
        }
    }
}

void DrivingImageWindow::showPage(int index, int drivingModeOverride)
{
    if (!m_stack) {
        return;
    }
    m_stack->setCurrentIndex(index);
    if (m_navBar) {
        m_navBar->setActiveTab(static_cast<DrivingImageNavBar::Tab>(index));
    }

    if (index == 0) {
        cancelPreviewChromeAutoHide();
        m_previewChromeVisible = false;
        const int mode =
            drivingModeOverride >= 0 ? drivingModeOverride : automotiveLayoutForUserOpen();
        setDrivingMode(mode);
        layoutTextOverlays();
        layoutCenterHint();
        startPreviewIfNeeded();
    } else if (m_ahdManager) {
        cancelPreviewChromeAutoHide();
        m_previewChromeVisible = false;
        m_ahdManager->stopPreview();
        if (index == 2 && m_playbackPage) {
            m_playbackPage->reloadDates();
        }
    }
    updatePreviewChrome();
}

void DrivingImageWindow::showPlaybackPage()
{
    if (!m_stack) {
        return;
    }
    m_stack->setCurrentIndex(2);
    if (m_navBar) {
        m_navBar->setActiveTab(DrivingImageNavBar::TabPlayback);
    }
    if (m_ahdManager) {
        cancelPreviewChromeAutoHide();
        m_previewChromeVisible = false;
        m_ahdManager->stopPreview();
    }
    updatePreviewChrome();
    show();
    raise();
    activateWindow();
}

void DrivingImageWindow::applyAutomotiveMode(int mode)
{
    if (!m_stack || m_stack->currentIndex() != 0) {
        showPage(0, mode);
        return;
    }
    setDrivingMode(mode);
    if (isVisible()) {
        layoutTextOverlays();
        layoutCenterHint();
        startPreviewIfNeeded();
        updatePreviewChrome();
    }
}

void DrivingImageWindow::layoutTextOverlays()
{
    if (!m_previewWrap || !m_safetyTipFrame) {
        return;
    }

    if (m_previewWrap->width() < 120 || m_previewWrap->height() < 80) {
        QTimer::singleShot(0, this, [this]() { layoutTextOverlays(); });
        return;
    }

    const int w = m_previewWrap->width();
    const int h = m_previewWrap->height();
    const int tipW = qMax(48, w * 60 / 1280);
    const int tipTop = h * 173 / 720;
    const int tipH = qMax(132, h * 400 / 720);
    m_safetyTipFrame->setGeometry(0, tipTop, tipW, tipH);
    m_safetyTipFrame->raise();

    // 原型 padding 32px 8px；图标 40×40，略减间距为文字留出底部空间
    const int padTop = tipH * 28 / 400;
    const int padH = qMax(4, tipW * 8 / 60);
    const int padBottom = tipH * 10 / 400;
    const int iconSide = qMax(20, tipW * 40 / 60);
    const int iconX = qMax(0, tipW * 10 / 60);
    const int iconGap = tipH * 20 / 400;
    m_safetyTipIcon->setGeometry(iconX, padTop, iconSide, iconSide);
    const int textTop = padTop + iconSide + iconGap;
    const int textAvailH = tipH - textTop - padBottom;
    int fontPx = qMax(18, tipW * 30 / 60);
    QFont tipFont;
    tipFont.setBold(true);
    const int textLines = 7;
    int textNeedH = 0;
    do {
        tipFont.setPixelSize(fontPx);
        textNeedH = verticalTipTextHeight(tipFont, textLines);
        if (textNeedH <= textAvailH || fontPx <= 16) {
            break;
        }
        --fontPx;
    } while (true);
    m_safetyTipText->setFont(tipFont);
    m_safetyTipText->setGeometry(padH, textTop, tipW - 2 * padH, qMax(textNeedH, textAvailH));
    m_safetyTipText->setStyleSheet(
        QStringLiteral("QLabel{color:#E3D948;background:transparent;border:none;padding:0px;}"));
    m_safetyTipFrame->show();

    if (m_exitHintLabel) {
        m_exitHintLabel->raise();
    }
    if (m_longPressHintLabel && m_longPressHintLabel->isVisible()) {
        layoutLongPressHint();
    }
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

void DrivingImageWindow::updatePreviewLayout()
{
    if (!m_ahdManager) {
        return;
    }
    m_ahdManager->setLayoutMode(m_cameraMode);
    m_ahdManager->setPreviewCameraIndex(m_isFullscreen ? m_fullscreenCameraId : -1);
}

void DrivingImageWindow::startPreviewIfNeeded()
{
    if (m_exitInProgress || !isVisible()) {
        return;
    }

    if (qEnvironmentVariableIsSet("CARDESK_SKIP_AHD")) {
        qWarning() << "[Driving] CARDESK_SKIP_AHD=1, skip camera preview (debug)";
        if (m_exitHintLabel) {
            m_exitHintLabel->setText(QStringLiteral("调试: 已跳过摄像头"));
            layoutCenterHint();
            m_exitHintLabel->show();
        }
        return;
    }

    qDebug() << "[Driving] startPreviewIfNeeded: opening cameras";
    updatePreviewLayout();
    if (m_exitHintLabel) {
        m_exitHintLabel->hide();
    }

    const QRect rect = previewRectOnScreen();
    if (rect.width() <= 0 || rect.height() <= 0) {
        m_exitHintLabel->setText(QStringLiteral("预览区域无效"));
        layoutCenterHint();
        m_exitHintLabel->show();
        return;
    }

    ahdManager()->enableSafetyWatermark(QStringLiteral("请注意周边安全"));

    if (!ahdManager()->startPreview(m_previewWrap, rect.x(), rect.y(), rect.width(), rect.height())) {
        m_exitHintLabel->setText(QStringLiteral("影像功能出现故障"));
        layoutCenterHint();
        m_exitHintLabel->show();
    } else {
        if (m_exitHintLabel) {
            m_exitHintLabel->hide();
        }
        layoutTextOverlays();
        layoutCenterHint();
    }
    updatePreviewChrome();
}

void DrivingImageWindow::stopPreview()
{
    if (m_ahdManager) {
        m_ahdManager->stopPreview();
    }
}

void DrivingImageWindow::setDrivingMode(int mode)
{
    const bool modeChanged = mode != m_cameraMode;
    if (mode == m_cameraMode && !m_isFullscreen) {
        updatePreviewChrome();
        return;
    }
    m_cameraMode = mode;
    m_isFullscreen = false;
    m_fullscreenCameraId = -1;
    if (modeChanged) {
        cancelPreviewChromeAutoHide();
        m_previewChromeVisible = false;
    }
    if (isVisible()) {
        updatePreviewLayout();
        if (m_ahdManager && m_ahdManager->isCameraReady()) {
            startPreviewIfNeeded();
        }
    }
    updatePreviewChrome();
}

int DrivingImageWindow::drivingMode() const
{
    return m_cameraMode;
}

QRect DrivingImageWindow::previewRectOnScreen() const
{
    QWidget *host = centralWidget();
    if (host && host->width() > 0 && host->height() > 0) {
        const QPoint topLeft = host->mapToGlobal(QPoint(0, 0));
        return QRect(topLeft, host->size());
    }

    QWidget *target = m_previewWrap ? static_cast<QWidget *>(m_previewWrap)
                                    : const_cast<DrivingImageWindow *>(this);
    if (!target || target->width() <= 0 || target->height() <= 0) {
        const DeviceDetect &device = DeviceDetect::instance();
        if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT
            && QApplication::primaryScreen()) {
            return QApplication::primaryScreen()->geometry();
        }
        return QRect();
    }

    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    return QRect(topLeft, target->size());
}

void DrivingImageWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_HomePage:
    case Qt::Key_Back:
    case Qt::Key_Escape:
        if (m_singleClickTimer->isActive()) {
            m_singleClickTimer->stop();
            m_lastClickMs = 0;
        }
        if (m_longPressTimer && m_longPressTimer->isActive()) {
            m_longPressTimer->stop();
        }
        event->accept();
        returnToMainSafely();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void DrivingImageWindow::showEvent(QShowEvent *event)
{
    qDebug() << "[Driving] showEvent begin";
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

    updatePreviewChrome();
    layoutCenterHint();
    layoutTextOverlays();
    QTimer::singleShot(0, this, [this]() {
        updatePreviewChrome();
        layoutCenterHint();
        layoutTextOverlays();
    });
    m_returning = false;
    m_exitInProgress = false;
    m_isFullscreen = false;
    m_fullscreenCameraId = -1;
    if ((!m_stack || m_stack->currentIndex() == 0) && !m_startScheduled) {
        m_startScheduled = true;
        QTimer::singleShot(100, this, [this]() {
            m_startScheduled = false;
            qDebug() << "[Driving] showEvent timer: startPreviewIfNeeded";
            if (!m_stack || m_stack->currentIndex() == 0) {
                startPreviewIfNeeded();
            }
        });
    }
    if (m_ahdManager) {
        m_ahdManager->syncRecordingWithSettings();
    }
    qDebug() << "[Driving] showEvent end";
}

void DrivingImageWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewChrome();
    if (m_stack && m_stack->currentIndex() == 0) {
        layoutTextOverlays();
        layoutCenterHint();
    }
    if (m_ahdManager && m_ahdManager->isPreviewActive() && m_previewWrap) {
        const QRect rect = previewRectOnScreen();
        m_ahdManager->startPreview(m_previewWrap, rect.x(), rect.y(), rect.width(), rect.height());
        updatePreviewChrome();
    }
}
