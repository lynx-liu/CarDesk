#include "mainwindow.h"
#include "pagebgwidget.h"
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
#include "videolistwindow.h"
#include "musicplayerwindow.h"
#include "topbarwidget.h"
#include "t507sdkbridge.h"

#include <QApplication>
#include <QKeyEvent>
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
    , m_topBar(nullptr)
    , m_navBar(nullptr)
    , m_centralWidget(nullptr)
    , m_transitionOverlay(nullptr)
    , m_volumeWidget(nullptr)
    , m_bluetoothManager(new BluetoothManager(this))
    , m_mediaManager(new MediaManager(this))
    , m_phoneWindow(nullptr)
    , m_radioWindow(nullptr)
    , m_diagnosticWindow(nullptr)
    , m_systemSettingWindow(nullptr)
    , m_drivingImageWindow(nullptr)
    , m_imageViewingWindow(nullptr)
{
    m_mediaManager->setBluetoothManager(m_bluetoothManager);
    setupWindowSize();
    setupUI();
    adjustForDevice();
    setupConnections();
    setupSystemInfo();

    // 启动即后台预热：解码背景图 + 预创建所有子页面窗口，
    // 确保用户首次点击时无需等待构建控件树。
    m_drivingImageWindow = new DrivingImageWindow();
    m_drivingImageWindow->setAttribute(Qt::WA_DeleteOnClose);
    QTimer::singleShot(0, this, [this]() {
        PageBgWidget::prewarm();
        if (m_drivingImageWindow) {
            m_drivingImageWindow->warmupCamera();
        }
        ensurePhoneWindow();
        // RadioWindow 构造函数会打开硬件设备并切换声道，不能后台预创建
        ensureDiagnosticWindow();
        ensureSystemSettingWindow();
        ensureImageViewingWindow();
    });
}

MainWindow::~MainWindow() {
}

MediaManager *MainWindow::mediaManager() const {
    return m_mediaManager;
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
    if (m_bluetoothManager) {
        const bool initConnected = m_bluetoothManager->isConnected();
        qApp->setProperty("appBluetoothConnected", initConnected);
        AppSignals::instance()->bluetoothConnectedChanged(initConnected);

        connect(m_bluetoothManager, &BluetoothManager::deviceConnected,
                AppSignals::instance(), [](const QString &) {
                    qApp->setProperty("appBluetoothConnected", true);
                    AppSignals::instance()->bluetoothConnectedChanged(true);
                });
        connect(m_bluetoothManager, &BluetoothManager::deviceDisconnected,
                AppSignals::instance(), []() {
                    qApp->setProperty("appBluetoothConnected", false);
                    AppSignals::instance()->bluetoothConnectedChanged(false);
                });

        connect(m_bluetoothManager, &BluetoothManager::callStatusChanged,
                this, &MainWindow::onBluetoothCallStatusChanged);

        // 启动时预先查询蓝牙连接状态，避免只有进入设置页面时才刷新菜单栏状态
        m_bluetoothManager->queryConnectedDevice();
    }
}

void MainWindow::setupSystemInfo() {
    // TopBarRightWidget 管理时钟与音量显示，无需主界面重复处理
}

void MainWindow::onBluetoothClicked() {
    qDebug() << "Bluetooth button clicked";
    m_bluetoothManager->scanDevices();
}

void MainWindow::onVideoListClicked() {
    qDebug() << "Video List button clicked";
    if (m_mediaManager && m_mediaManager->musicWindow()) {
        m_mediaManager->musicWindow()->pauseForInterruption();
    }
    if (m_mediaManager) {
        m_mediaManager->prepareForNonBluetoothAudio();
    }
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
    if (m_mediaManager) {
        m_mediaManager->prepareForNonBluetoothAudio();
    }
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

void MainWindow::ensurePhoneWindow() {
    if (!m_phoneWindow) {
        m_phoneWindow = new PhoneWindow(m_bluetoothManager, m_mediaManager);
        connect(m_phoneWindow, &PhoneWindow::requestReturnToMain, this, [this]() {
            qDebug() << "MainWindow received PhoneWindow requestReturnToMain";
            restorePreviousWindow();
            if (m_phoneWindow) {
                m_phoneWindow->hide();
            }
        }, Qt::UniqueConnection);
        connect(m_phoneWindow, &QObject::destroyed, this, [this]() {
            m_phoneWindow = nullptr;
            restorePreviousWindow();
        }, Qt::UniqueConnection);
    }
}

void MainWindow::onPhoneClicked() {
    qDebug() << "Phone button clicked";
    if (m_mediaManager) {
        const bool radioActive = (m_radioWindow && m_radioWindow->isAudioActive());
        // 手动切到拨号界面时，收音机允许后台继续播放；
        // 真正来电/通话中断仍由 onBluetoothCallStatusChanged() 处理。
        if (!radioActive) {
            m_mediaManager->pausePlaybackForInterruption();
        }
    }
    m_restoreStack.clear();
    ensurePhoneWindow();
    this->hide();
    m_phoneWindow->show();
    m_phoneWindow->raise();
    m_phoneWindow->activateWindow();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Phone) {
        qDebug() << "MainWindow keyPressEvent Key_Phone => open phone";
        onPhoneClicked();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::onBluetoothCallStatusChanged(int status) {
    if (status == 1) {
        // 通话结束
        if (!m_restoreStack.isEmpty()) {
            // 如果我们之前为通话推入了恢复目标，隐藏 PhoneWindow 并恢复栈顶
            if (m_phoneWindow && m_phoneWindow->isVisible()) {
                m_phoneWindow->hide();
            }
            restorePreviousWindow();
        }
        if (m_mediaManager) {
            m_mediaManager->resumePlaybackAfterInterruption();
        }
        // 如果栈为空，则表示 PhoneWindow 本来就在前台，保留 PhoneWindow，PhoneWindow 自身会恢复到之前的 tab
        return;
    }

    if (status != 4 && status != 5 && status != 6) {
        return;
    }

    if (m_mediaManager) {
        m_mediaManager->pausePlaybackForInterruption();
    }

    QWidget *current = findCurrentVisibleNonPhoneWindow();
    if (current && current == m_drivingImageWindow) {
        return;
    }
    // 记录当前界面到恢复栈（仅当当前不是 PhoneWindow 时）
    if (current && current != m_phoneWindow) {
        m_restoreStack.append({QPointer<QWidget>(current)});
        current->hide();
    } else if (!current && this->isVisible()) {
        m_restoreStack.append({QPointer<QWidget>(this)});
        this->hide();
    }
    ensurePhoneWindow();
    this->hide();
    m_phoneWindow->show();
    m_phoneWindow->raise();
    m_phoneWindow->activateWindow();
}

void MainWindow::ensureRadioWindow() {
    if (!m_radioWindow) {
        m_radioWindow = new RadioWindow();
        // 保持窗口常驻，hide/show 复用，避免每次重建子控件
        connect(m_radioWindow, &RadioWindow::requestReturnToMain, this, [this]() {
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
        connect(m_radioWindow, &QObject::destroyed, this, [this]() {
            if (m_mediaManager) {
                m_mediaManager->setRadioWindow(nullptr);
            }
            m_radioWindow = nullptr;
            this->show();
            this->raise();
            this->activateWindow();
        }, Qt::UniqueConnection);
    }
    if (m_mediaManager) {
        m_mediaManager->setRadioWindow(m_radioWindow);
    }
}

void MainWindow::onRadioClicked() {
    qDebug() << "Radio button clicked";

    ensureRadioWindow();

    if (m_mediaManager) {
        m_mediaManager->prepareForRadioAudio();
    }
    this->hide();
    m_radioWindow->show();
    m_radioWindow->raise();
    m_radioWindow->activateWindow();
}

void MainWindow::ensureDiagnosticWindow() {
    if (!m_diagnosticWindow) {
        m_diagnosticWindow = new DiagnosticWindow();
        // 保持窗口常驻，hide/show 复用，避免每次重建子控件
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
}

void MainWindow::onDiagnosticClicked() {
    qDebug() << "Diagnostic button clicked";

    ensureDiagnosticWindow();

    this->hide();
    m_diagnosticWindow->show();
    m_diagnosticWindow->raise();
    m_diagnosticWindow->activateWindow();
}

void MainWindow::ensureSystemSettingWindow() {
    if (!m_systemSettingWindow) {
        m_systemSettingWindow = new SystemSettingWindow(m_bluetoothManager);
        // 保持窗口常驻，hide/show 复用，避免每次重建子控件
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
}

void MainWindow::onSystemSettingsClicked() {
    qDebug() << "System settings button clicked";

    ensureSystemSettingWindow();

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
        if (m_mediaManager) {
            m_mediaManager->resumePlaybackAfterInterruption();
        }
        this->show();
        this->raise();
        this->activateWindow();
    }, Qt::UniqueConnection);
    connect(m_drivingImageWindow, &QObject::destroyed, this, [this]() {
        m_drivingImageWindow = nullptr;
        if (m_mediaManager) {
            m_mediaManager->resumePlaybackAfterInterruption();
        }
        this->show();
        this->raise();
        this->activateWindow();
    }, Qt::UniqueConnection);

    if (m_mediaManager) {
        m_mediaManager->pausePlaybackForOcclusion();
    }

    // 现在行车影像改为 Qt 内部自己绘制，不再依赖外部视频硬件层。
    m_drivingImageWindow->show();
    m_drivingImageWindow->raise();
    m_drivingImageWindow->activateWindow();
    this->hide();
}

QWidget *MainWindow::findCurrentVisibleNonPhoneWindow() const {
    if (m_mediaManager && m_mediaManager->videoListWindow()) {
        if (m_mediaManager->videoListWindow()->videoPlayWindow()
                && m_mediaManager->videoListWindow()->videoPlayWindow()->isVisible()) {
            return m_mediaManager->videoListWindow()->videoPlayWindow();
        }
        if (m_mediaManager->videoListWindow()->isVisible()) {
            return m_mediaManager->videoListWindow();
        }
    }
    if (m_mediaManager && m_mediaManager->musicWindow() && m_mediaManager->musicWindow()->isVisible()) {
        return m_mediaManager->musicWindow();
    }
    if (m_radioWindow && m_radioWindow->isVisible()) {
        return m_radioWindow;
    }
    if (m_diagnosticWindow && m_diagnosticWindow->isVisible()) {
        return m_diagnosticWindow;
    }
    if (m_systemSettingWindow && m_systemSettingWindow->isVisible()) {
        return m_systemSettingWindow;
    }
    if (m_drivingImageWindow && m_drivingImageWindow->isVisible()) {
        return m_drivingImageWindow;
    }
    if (m_imageViewingWindow && m_imageViewingWindow->isVisible()) {
        return m_imageViewingWindow;
    }
    if (this->isVisible()) {
        return const_cast<MainWindow *>(this);
    }
    return nullptr;
}

void MainWindow::restorePreviousWindow() {
    qDebug() << "restorePreviousWindow: stack size=" << m_restoreStack.size();
    if (!m_restoreStack.isEmpty()) {
        RestoreState st = m_restoreStack.takeLast();
        QWidget *restore = st.widget;
        if (restore && !restore->isVisible()) {
            qDebug() << "restorePreviousWindow: showing restore target" << restore->metaObject()->className();
            restore->show();
        }
        if (restore) {
            qDebug() << "restorePreviousWindow: raising restore target" << restore->metaObject()->className();
            restore->raise();
            restore->activateWindow();
            return;
        }
    }
    qDebug() << "restorePreviousWindow: fallback to MainWindow";
    this->show();
    this->raise();
    this->activateWindow();
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

void MainWindow::ensureImageViewingWindow() {
    if (!m_imageViewingWindow) {
        m_imageViewingWindow = new ImageViewingWindow();
        // 保持窗口常驻，hide/show 复用，避免每次重建子控件
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
}

void MainWindow::onImageViewingClicked() {
    qDebug() << "Image viewing button clicked";

    ensureImageViewingWindow();

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
