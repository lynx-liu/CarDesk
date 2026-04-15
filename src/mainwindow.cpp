#include "mainwindow.h"
#include "devicedetect.h"
#include "appsignals.h"
#include "bluetoothmanager.h"
#include "mediamanager.h"
#include "phonewindow.h"
#include "radiowindow.h"
#include "diagnosticwindow.h"
#include "systemsettingwindow.h"
#include "drivingimagewindow.h"
#include "imageviewingwindow.h"
#include "usbmanager.h"
#include "videolistwindow.h"
#include "musicplayerwindow.h"
#include "topbarwidget.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QScreen>
#include <QDebug>
#include <QIcon>
#include <QGridLayout>
#include <QProcess>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_clockLabel(nullptr)
    , m_volumeLabel(nullptr)
    , m_volumeWidget(nullptr)
    , m_volBtn(nullptr)
    , m_isMuted(false)
    , m_transitionOverlay(nullptr)
    , m_bluetoothManager(new BluetoothManager(this))
    , m_mediaManager(new MediaManager(this))
    , m_phoneWindow(nullptr)
    , m_radioWindow(nullptr)
    , m_diagnosticWindow(nullptr)
    , m_systemSettingWindow(nullptr)
    , m_drivingImageWindow(nullptr)
    , m_imageViewingWindow(nullptr)
    , m_usbManager(new USBManager(this))
    , m_clockTimer(new QTimer(this))
{
    setupWindowSize();
    setupUI();
    adjustForDevice();
    setupConnections();
    setupSystemInfo();

    // 启动即后台预热行车影像摄像头，减少首次进入延迟。
    m_drivingImageWindow = new DrivingImageWindow();
    m_drivingImageWindow->setAttribute(Qt::WA_DeleteOnClose);
    QTimer::singleShot(0, this, [this]() {
        if (m_drivingImageWindow) {
            m_drivingImageWindow->warmupCamera();
        }
    });
}

MainWindow::~MainWindow() {
}

void MainWindow::setupWindowSize() {
    const DeviceDetect &device = DeviceDetect::instance();
    
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        // 车机：全屏显示
        setWindowState(Qt::WindowFullScreen);
    } else {
        // PC：固定窗口大小 1280x720（与 index.html 一致）
        setFixedSize(1280, 720);
        if (QApplication::primaryScreen()) {
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
        }
    }
}

void MainWindow::setupUI() {
    // 创建中央 widget
    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("centralWidget");
    setCentralWidget(m_centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // 创建顶部栏
    createTopBar();
    mainLayout->addWidget(m_topBar);
    
    // 创建导航栏
    createNavigationBar();
    mainLayout->addWidget(m_navBar, 1);
    
    applyIndexStyle();
        ensureTransitionOverlay();
    
    setWindowTitle("CarDesk");
}

void MainWindow::applyIndexStyle() {
    // 使用 index.html 的贴图与布局尺寸
    const QString style = R"(
        QWidget#centralWidget {
            background-image: url(:/images/background.png);
            background-position: center;
            background-repeat: no-repeat;
        }

        QWidget#topBar {
            background-image: url(:/images/topbar.png);
            background-repeat: no-repeat;
        }

        QLabel#titleLabel {
            color: #ffffff;
            font-size: 36px;
            background: transparent;
            font-weight: 700;
        }

        QLabel#clockLabel {
            color: #ffffff;
            font-size: 36px;
            background: transparent;
        }

        QLabel#volumeLabel {
            color: #ffffff;
            font-size: 36px;
            background: transparent;
        }

        QPushButton#btBtn {
            border: none;
            width: 48px;
            height: 48px;
            background-image: url(:/images/pict_buetooth.png);
        }
        QPushButton#btBtn:hover {
            background-image: url(:/images/pict_buetooth_on.png);
        }

        QPushButton#usbBtn {
            border: none;
            width: 48px;
            height: 48px;
            background-image: url(:/images/pict_usb.png);
        }
        QPushButton#usbBtn:hover {
            background-image: url(:/images/pict_usb_on.png);
        }

        QPushButton[nav="true"] {
            border: none;
            width: 216px;
            height: 271px;
        }
        QPushButton.navBtn:pressed {
            padding-top: 1px;
        }
    )";

    m_centralWidget->setStyleSheet(style);
}

void MainWindow::createTopBar() {
    m_topBar = new QWidget(this);
    m_topBar->setObjectName("topBar");
    m_topBar->setFixedHeight(82);
    
    QGridLayout *topLayout = new QGridLayout(m_topBar);
    topLayout->setContentsMargins(16, 0, 16, 0);
    topLayout->setColumnStretch(0, 1);
    topLayout->setColumnStretch(1, 0);
    topLayout->setColumnStretch(2, 1);

    // 中间标题
    QLabel *titleLabel = new QLabel("主界面", this);
    titleLabel->setObjectName("titleLabel");
    topLayout->addWidget(titleLabel, 0, 1, Qt::AlignCenter);

    // 右侧状态图标（使用 TopBarRightWidget 统一管理音量/时钟/状态）
    auto *topBarRight = new TopBarRightWidget(this);
    topLayout->addWidget(topBarRight, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
}

void MainWindow::createNavigationBar() {
    m_navBar = new QWidget(this);
    m_navBar->setObjectName("navBar");
    
    QGridLayout *navLayout = new QGridLayout(m_navBar);
    navLayout->setContentsMargins(118, 34, 118, 34);
    navLayout->setHorizontalSpacing(60);
    navLayout->setVerticalSpacing(34);
    
    struct NavItem {
        int row, col;
        QString upImage;
        QString downImage;
        const char *slot;
    };

    NavItem items[] = {
        {0, 0, ":/images/butt_home_radio_up.png", ":/images/butt_home_radio_down.png", SLOT(onRadioClicked())},
        {0, 1, ":/images/butt_home_driving_image_up.png", ":/images/butt_home_driving_image_down.png", SLOT(onDrivingImageClicked())},
        {0, 2, ":/images/butt_home_video_play_up.png", ":/images/butt_home_video_play_down.png", SLOT(onVideoListClicked())},
        {0, 3, ":/images/butt_home_image_viewing_up.png", ":/images/butt_home_image_viewing_down.png", SLOT(onImageViewingClicked())},
        {1, 0, ":/images/butt_home_diagnostic_maintenance_up.png", ":/images/butt_home_diagnostic_maintenance_down.png", SLOT(onDiagnosticClicked())},
        {1, 1, ":/images/butt_home_bluetooth_phone_up.png", ":/images/butt_home_bluetooth_phone_down.png", SLOT(onPhoneClicked())},
        {1, 2, ":/images/butt_home_audio_play_up.png", ":/images/butt_home_audio_play_down.png", SLOT(onMusicUSBClicked())},
        {1, 3, ":/images/butt_home_system_settings_up.png", ":/images/butt_home_system_settings_down.png", SLOT(onSystemSettingsClicked())},
    };

    for (const auto &item : items) {
        QPushButton *btn = new QPushButton(this);
        btn->setProperty("nav", true);
        btn->setFixedSize(216, 271);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString(
            "QPushButton { border: none; background-image: url(%1); }"
            "QPushButton:hover { background-image: url(%2); }"
        ).arg(item.upImage, item.downImage));

        if (item.slot) {
            connect(btn, SIGNAL(clicked()), this, item.slot);
        }

        navLayout->addWidget(btn, item.row, item.col, Qt::AlignCenter);
    }
}

void MainWindow::adjustForDevice() {
    const DeviceDetect &device = DeviceDetect::instance();
    
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        // 车机模式调整
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        qDebug() << "Device configured for Car Unit (T507)";
    } else {
        // PC 模式调整
        setWindowTitle("CarDesk - PC Mode");
        qDebug() << "Device configured for PC Ubuntu";
    }
}

void MainWindow::setupConnections() {
    // TopBarRightWidget 自行管理时钟和音量显示 / 响应 AppSignals，无需在主界面重复处理
}

void MainWindow::setupSystemInfo() {
    // 更新系统时间
    onUpdateClock();
    
    // 启动时钟定时器（每秒更新）
    m_clockTimer->start(1000);
    
    // 初始音量由 TopBarRightWidget 管理
}

void MainWindow::onUpdateClock() {
    if (!m_clockLabel) return;
    
    QDateTime now = QDateTime::currentDateTime();
    QString timeStr = now.toString(AppSignals::timeFormat());
    m_clockLabel->setText(timeStr);
}

void MainWindow::onBluetoothClicked() {
    qDebug() << "Bluetooth button clicked";
    m_bluetoothManager->scanDevices();
}

void MainWindow::onUSBClicked() {
    qDebug() << "USB button clicked";
    m_usbManager->scanDevices();
}

void MainWindow::onVolumeClicked() {
    qDebug() << "Volume button clicked";
    m_isMuted = !m_isMuted;
    // 切换图标
    if (m_volBtn) {
        const QString icon = m_isMuted
            ? QStringLiteral(":/images/pict_volume_mute.png")
            : QStringLiteral(":/images/pict_volume.png");
        m_volBtn->setStyleSheet(
            QString("QPushButton { border: none; background-image: url(%1); "
                    "background-repeat: no-repeat; background-position: center; }").arg(icon));
    }
    // 静音时隐藏数字（固定宽度保持布局稳定）；取消静音时显示实际等级
    if (m_volumeLabel) {
        if (m_isMuted) {
            m_volumeLabel->setText("");
        } else {
            const QVariant vp = qApp->property("appVolumeLevel");
            m_volumeLabel->setText(QString::number(vp.isValid() ? vp.toInt() : 10));
        }
    }
}

void MainWindow::onVideoListClicked() {
    qDebug() << "Video List button clicked";
    m_mediaManager->openVideoList();

    if (auto *listWindow = m_mediaManager->videoListWindow()) {
        connect(listWindow, &VideoListWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(listWindow, &QObject::destroyed, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        this->hide();
    }
}

void MainWindow::onMusicUSBClicked() {
    qDebug() << "Music USB button clicked";
    m_mediaManager->openMusicPlayer();
    
    // 连接音频窗口的返回信号
    if (m_mediaManager->musicWindow()) {
        connect(m_mediaManager->musicWindow(), &MusicPlayerWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_mediaManager->musicWindow(), &QObject::destroyed, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        this->hide();
    }
}

void MainWindow::onPhoneClicked() {
    qDebug() << "Phone button clicked";

    if (!m_phoneWindow) {
        m_phoneWindow = new PhoneWindow();
        m_phoneWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_phoneWindow, &PhoneWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_phoneWindow, &QObject::destroyed, this, [this]() {
            m_phoneWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }

    this->hide();
    m_phoneWindow->show();
    m_phoneWindow->raise();
    m_phoneWindow->activateWindow();
}

void MainWindow::onRadioClicked() {
    qDebug() << "Radio button clicked";

    if (!m_radioWindow) {
        m_radioWindow = new RadioWindow();
        m_radioWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_radioWindow, &RadioWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_radioWindow, &QObject::destroyed, this, [this]() {
            m_radioWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }

    this->hide();
    m_radioWindow->show();
    m_radioWindow->raise();
    m_radioWindow->activateWindow();
}

void MainWindow::onDiagnosticClicked() {
    qDebug() << "Diagnostic button clicked";

    if (!m_diagnosticWindow) {
        m_diagnosticWindow = new DiagnosticWindow();
        m_diagnosticWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_diagnosticWindow, &DiagnosticWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_diagnosticWindow, &QObject::destroyed, this, [this]() {
            m_diagnosticWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }

    this->hide();
    m_diagnosticWindow->show();
    m_diagnosticWindow->raise();
    m_diagnosticWindow->activateWindow();
}

void MainWindow::onSystemSettingsClicked() {
    qDebug() << "System settings button clicked";

    if (!m_systemSettingWindow) {
        m_systemSettingWindow = new SystemSettingWindow();
        m_systemSettingWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_systemSettingWindow, &SystemSettingWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_systemSettingWindow, &QObject::destroyed, this, [this]() {
            m_systemSettingWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }

    this->hide();
    m_systemSettingWindow->show();
    m_systemSettingWindow->raise();
    m_systemSettingWindow->activateWindow();
}

void MainWindow::onDrivingImageClicked() {
    qDebug() << "Driving image button clicked";

    if (!m_drivingImageWindow) {
        m_drivingImageWindow = new DrivingImageWindow();
        m_drivingImageWindow->setAttribute(Qt::WA_DeleteOnClose);
    }

    connect(m_drivingImageWindow, &DrivingImageWindow::requestReturnToMain, this, [this]() {
        this->show();
        this->raise();
        this->activateWindow();
    }, Qt::UniqueConnection);
    connect(m_drivingImageWindow, &QObject::destroyed, this, [this]() {
        m_drivingImageWindow = nullptr;
        this->show();
        this->raise();
        this->activateWindow();
    }, Qt::UniqueConnection);

    // 现在行车影像改为 Qt 内部自己绘制，不再依赖外部视频硬件层。
    m_drivingImageWindow->show();
    m_drivingImageWindow->raise();
    m_drivingImageWindow->activateWindow();
    this->hide();
}

void MainWindow::forceMainInterfaceRedraw()
{
    // 对所有可见子控件递归同步重绘，覆盖 T507 双缓冲的两张 buffer：
    // 第一轮写入当前 buffer，第二轮（60ms 后）写入另一张 buffer，
    // 确保后续任意 buffer 切换时不出现黑屏。
    auto repaintAll = [this]() {
        const QList<QWidget *> children = findChildren<QWidget *>();
        for (QWidget *w : children) {
            if (w->isVisible())
                w->repaint();
        }
        repaint();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 30);
    };

    repaintAll();                             // 立即刷新第一张 buffer
    QTimer::singleShot(60, this, repaintAll); // 60ms 后刷新第二张 buffer
}

void MainWindow::ensureTransitionOverlay()
{
    if (m_transitionOverlay) {
        m_transitionOverlay->setGeometry(rect());
        return;
    }

    m_transitionOverlay = new QWidget(this);
    m_transitionOverlay->setObjectName("transitionOverlay");
    m_transitionOverlay->setStyleSheet("background-color: #000000;");
    m_transitionOverlay->setGeometry(rect());
    m_transitionOverlay->hide();
}

void MainWindow::showTransitionOverlay()
{
    ensureTransitionOverlay();
    m_transitionOverlay->setGeometry(rect());
    m_transitionOverlay->show();
    m_transitionOverlay->raise();
    m_transitionOverlay->repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
}

void MainWindow::hideTransitionOverlay()
{
    if (!m_transitionOverlay) {
        return;
    }

    m_transitionOverlay->hide();
    repaint();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_transitionOverlay) {
        m_transitionOverlay->setGeometry(rect());
    }
}

void MainWindow::onImageViewingClicked() {
    qDebug() << "Image viewing button clicked";

    if (!m_imageViewingWindow) {
        m_imageViewingWindow = new ImageViewingWindow();
        m_imageViewingWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_imageViewingWindow, &ImageViewingWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_imageViewingWindow, &QObject::destroyed, this, [this]() {
            m_imageViewingWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }

    this->hide();
    m_imageViewingWindow->show();
    m_imageViewingWindow->raise();
    m_imageViewingWindow->activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    qDebug() << "Application closing";
    event->accept();
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event) {
    const DeviceDetect &device = DeviceDetect::instance();
    
    // 车机上禁用右键菜单
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        event->ignore();
        return;
    }
    
    QMainWindow::contextMenuEvent(event);
}
