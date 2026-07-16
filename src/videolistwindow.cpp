#include "videolistwindow.h"
#include "pagebgwidget.h"
#include "videoplaywindow.h"
#include "musicplayerwindow.h"
#include "devicedetect.h"
#include "topbarwidget.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QHBoxLayout>

static const QString kUsbMountDir    = QStringLiteral("/mnt/usb");
static const QString kUsbMountPrefix = QStringLiteral("/mnt/usb/");
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QScreen>
#include <QApplication>
#include <QFileInfo>
#include <QListWidget>
#include <QProcess>
#include <QDateTime>
#include <QCloseEvent>
#include <QSize>
#include <QStorageInfo>
#include "appsignals.h"

static QString findFirstUsbVideoDirectory();

VideoListWindow::VideoListWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_homeButton(new QPushButton(this))
    , m_backDirButton(new QPushButton(this))
    , m_videoListWidget(new QListWidget(this))
    , m_pathLabel(new QLabel("USB > 电影 > 欧美", this))
    , m_currentPath("/mnt")
    , m_initialPath("/mnt")
{
    setWindowTitle("视频播放");
    setFixedSize(1280, 720);
    
    m_videoExtensions << "*.mp4" << "*.mkv" << "*.avi" << "*.mov" << "*.flv" << "*.wmv" << "*.webm" << "*.m4v";
    
    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else {
        if (QApplication::primaryScreen()) {
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
        }
    }
    
    setupUI();

    connect(AppSignals::instance(), &AppSignals::usbStateChanged, this, [this](bool connected) {
        if (!connected) {
            m_currentPath.clear();
            if (m_videoListWidget) m_videoListWidget->clear();
            if (m_pathLabel) m_pathLabel->setText(QStringLiteral("请插入U盘"));
            // 拔盘停止 USB 视频（行车录像来自 TF，不受影响）
            if (m_playWindow) {
                m_playWindow->stopUsbPlayback();
            }
        } else if (isVisible()) {
            const QString usbPath = findFirstUsbVideoDirectory();
            if (!usbPath.isEmpty()) loadVideoFiles(usbPath);
        }
    });

    const QString initUsb = findFirstUsbVideoDirectory();
    if (!initUsb.isEmpty()) {
        m_currentPath = initUsb;
        loadVideoFiles(initUsb);
    } else {
        if (m_pathLabel) m_pathLabel->setText(QStringLiteral("请插入U盘"));
    }

    connect(m_homeButton, &QPushButton::clicked, this, &VideoListWindow::onHomeClicked);
    connect(m_backDirButton, &QPushButton::clicked, this, &VideoListWindow::onBackClicked);
    connect(m_videoListWidget, &QListWidget::itemClicked, this, &VideoListWindow::onItemClicked);
}

VideoListWindow::~VideoListWindow() {
    if (m_playWindow) {
        m_playWindow->deleteLater();
        m_playWindow = nullptr;
    }
}

void VideoListWindow::closeEvent(QCloseEvent *event) {
    // 窗口真正关闭时（卖程应用等）才发信号
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void VideoListWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    const QString usbPath = findFirstUsbVideoDirectory();
    if (usbPath.isEmpty()) {
        m_currentPath.clear();
        if (m_videoListWidget) m_videoListWidget->clear();
        if (m_pathLabel) m_pathLabel->setText(QStringLiteral("请插入U盘"));
        return;
    }

    const QString normalizedCurrentPath = QDir::cleanPath(m_currentPath);
    if (!normalizedCurrentPath.isEmpty()
            && (normalizedCurrentPath == usbPath || normalizedCurrentPath.startsWith(usbPath + '/'))
            && QFileInfo::exists(normalizedCurrentPath)) {
        loadVideoFiles(normalizedCurrentPath);
        return;
    }

    loadVideoFiles(usbPath);
}

void VideoListWindow::setupUI() {
    QWidget *centralWidget = new PageBgWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // ===== 顶部栏 - 与音乐/图片列表相同：绝对定位叠在 topBar 上 =====
    QWidget *topBar = new QWidget(centralWidget);
    topBar->setFixedSize(1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    m_homeButton->setParent(topBar);
    m_homeButton->setGeometry(12, 12, 48, 48);
    m_homeButton->setFocusPolicy(Qt::NoFocus);
    m_homeButton->setStyleSheet(
        "QPushButton { border:none; background-image:url(:/images/pict_home_up.png); background-repeat:no-repeat; }"
        "QPushButton:hover { background-image:url(:/images/pict_home_down.png); }"
    );
    m_homeButton->setCursor(Qt::PointingHandCursor);

    auto *titleLabel = new QLabel(QStringLiteral("视频播放"), topBar);
    titleLabel->setGeometry(0, 0, 1280, 72);
    titleLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_homeButton->raise();

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    mainLayout->addWidget(topBar);
    
    // ===== 返回按钮 =====
    QWidget *backWidget = new QWidget(this);
    backWidget->setFixedHeight(102);  // 60 + 21*2
    
    QHBoxLayout *backLayout = new QHBoxLayout(backWidget);
    backLayout->setContentsMargins(60, 21, 0, 21);  // margin: 21px 60px
    
    m_backDirButton->setFixedSize(60, 60);
    m_backDirButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-image: url(:/images/butt_back_up.png); "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/butt_back_down.png); "
        "}"
    );
    
    backLayout->addWidget(m_backDirButton);
    m_backDirButton->setFocusPolicy(Qt::NoFocus);
    backLayout->addStretch();
    
    mainLayout->addWidget(backWidget);
    
    // ===== 视频列表区域 =====
    // HTML: .music_list_con { width:944px; margin:24px auto 36px }
    // HTML: ul { display:flex; height:356px; flex-wrap:wrap;
    //           align-content:space-between; justify-content:space-between }
    // HTML: li { width:160px; height:160px }
    // 计算: 5列×160=800, (944-800)/4=36px水平间隙
    //       2行×160=320, (356-320)/1=36px垂直间隙
    //       gridSize = 160+36 = 196
    QWidget *listWidget = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 24, 0, 36);  // margin: 24px auto 36px
    listLayout->setSpacing(44);  // 路径与列表之间间距
    
    // 列表容器
    m_videoListWidget->setStyleSheet(
        "QListWidget { "
        "  background: transparent; "
        "  border: none; "
        "  outline: none; "
        "} "
        "QScrollBar:vertical{width:12px;background:transparent;border-radius:6px;margin:0; padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px; padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;background:none;border:none;}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}"
    );
    m_videoListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_videoListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_videoListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    
    // 精确匹配HTML flexbox space-between布局
    // 5列: 944/5 = 188.8px per cell → gridSize = 188
    // item 160px, 间距 = 188-160 = 28px（接近HTML的36px）
    // 2行: gridH 同样 188
    m_videoListWidget->setViewMode(QListView::IconMode);
    m_videoListWidget->setMovement(QListView::Static);
    m_videoListWidget->setFlow(QListView::LeftToRight);
    m_videoListWidget->setWrapping(true);
    m_videoListWidget->setSpacing(0);
    m_videoListWidget->setResizeMode(QListView::Fixed);
    m_videoListWidget->setIconSize(QSize(0, 0));
    m_videoListWidget->setGridSize(QSize(188, 178));  // 水平188×5=940
    m_videoListWidget->setFixedSize(956, 356);  // 保留足够空间显示垂直滚动条
    
    // 应用自定义委托来绘制背景和文本
    m_videoListWidget->setItemDelegate(new VideoListItemDelegate(m_videoListWidget));
    
    listLayout->addWidget(m_videoListWidget, 0, Qt::AlignHCenter);
    
    // 路径显示
    m_pathLabel->setStyleSheet(
        "background: rgba(255, 255, 255, .1); "
        "border: 1px solid #0068FF; "
        "border-radius: 5px; "
        "font-size: 24px; "
        "color: #fff; "
        "padding: 12px 24px;"
    );
    m_pathLabel->setFixedHeight(50);
    m_pathLabel->setFixedWidth(956);
    m_pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    
    listLayout->addWidget(m_pathLabel, 0, Qt::AlignHCenter);
    
    mainLayout->addWidget(listWidget, 1);
}

void VideoListWindow::loadVideoFiles(const QString &directory) {
    m_currentPath = directory;
    m_videoListWidget->clear();
    
    QDir dir(directory);
    dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::DirsFirst);
    
    const QString normalizedPath = QDir::cleanPath(directory);
    if (!dir.exists()) {
        if (!normalizedPath.startsWith(kUsbMountPrefix)) {
            m_currentPath.clear();
            m_videoListWidget->clear();
            if (m_pathLabel) m_pathLabel->setText(QStringLiteral("请插入U盘"));
            return;
        }
        // USB 路径不存在时继续，下方显示空列表
    }
    QFileInfoList fileInfos = dir.entryInfoList();
    qDebug() << "Loading directory:" << directory;
    qDebug() << "Found" << fileInfos.count() << "items";
    
    // 首先添加目录
    for (const QFileInfo &fileInfo : fileInfos) {
        if (fileInfo.isDir()) {
            // 创建item，纯文本，不使用图标
            QListWidgetItem *item = new QListWidgetItem(fileInfo.fileName(), m_videoListWidget);
            item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
            item->setData(Qt::UserRole + 1, true); // isDirectory
            item->setData(Qt::UserRole + 2, "folder");
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            
            m_videoListWidget->addItem(item);
            qDebug() << "Added folder:" << fileInfo.fileName();
        }
    }
    
    // 然后添加视频文件
    for (const QFileInfo &fileInfo : fileInfos) {
        if (fileInfo.isFile()) {
            bool isVideo = false;
            for (const QString &ext : m_videoExtensions) {
                if (fileInfo.fileName().endsWith(ext.mid(1))) {
                    isVideo = true;
                    break;
                }
            }
            
            if (isVideo) {
                QListWidgetItem *item = new QListWidgetItem(fileInfo.baseName(), m_videoListWidget);
                item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
                item->setData(Qt::UserRole + 1, false); // not directory
                item->setData(Qt::UserRole + 2, "file");
                item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
                
                m_videoListWidget->addItem(item);
                qDebug() << "Added video:" << fileInfo.baseName();
            }
        }
    }
    
    // USB 路径标签（sda1 为设备根）
    if (m_pathLabel) {
        QString deviceRoot;
        if (normalizedPath.startsWith(kUsbMountPrefix)) {
            int sep = normalizedPath.indexOf('/', 9);
            deviceRoot = (sep < 0) ? normalizedPath : normalizedPath.left(sep);
        }
        QString displayPath;
        if (!deviceRoot.isEmpty()) {
            displayPath = QStringLiteral("U盘");
            if (normalizedPath != deviceRoot) {
                QString rel = normalizedPath.mid(deviceRoot.length() + 1);
                if (!rel.isEmpty())
                    displayPath += QStringLiteral(" > ") + rel.replace('/', QStringLiteral(" > "));
            }
        } else {
            displayPath = normalizedPath;
        }
        if (m_videoListWidget->count() == 0)
            displayPath = QStringLiteral("无内容");
        m_pathLabel->setText(displayPath);
    }
    qDebug() << "Loaded" << m_videoListWidget->count() << "items in total";
}

void VideoListWindow::updatePath(const QString &path) {
    QString displayPath = path;
    if (displayPath == "/mnt") {
        displayPath = "/mnt";
    }
    m_pathLabel->setText(displayPath);
}

void VideoListWindow::onHomeClicked() {
    emit requestReturnToMain();
    hide();
}

void VideoListWindow::onBackClicked() {
    const QString normalizedPath = QDir::cleanPath(m_currentPath);
    QString deviceRoot;
    if (normalizedPath.startsWith(kUsbMountPrefix)) {
        int sep = normalizedPath.indexOf('/', 9);
        deviceRoot = (sep < 0) ? normalizedPath : normalizedPath.left(sep);
    }
    if (!deviceRoot.isEmpty()) {
        if (normalizedPath == deviceRoot) {
            emit requestReturnToMain();
            hide();
            return;
        }
        QDir dir(normalizedPath);
        if (dir.cdUp()) {
            const QString parent = dir.absolutePath();
            if (parent == deviceRoot || parent.startsWith(deviceRoot + '/')) {
                loadVideoFiles(parent);
                return;
            }
        }
        emit requestReturnToMain();
        hide();
        return;
    }
    // 非USB，直接返回主界面
    emit requestReturnToMain();
    hide();
}

void VideoListWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::changeVolume(+1);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::changeVolume(-1);
        break;
    case Qt::Key_HomePage:
        onHomeClicked();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        onBackClicked();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

bool VideoListWindow::tryResumeVideo(VideoPlaybackOrigin origin)
{
    if (m_playWindow && m_playWindow->hasPendingResumeFor(origin)) {
        if (m_musicWindow) {
            m_musicWindow->stopIfPlaying();
        }
        this->hide();
        m_playWindow->show();
        m_playWindow->raise();
        m_playWindow->activateWindow();
        return true;
    }
    return false;
}

bool VideoListWindow::pauseVideoIfPlaying()
{
    qDebug() << "VideoListWindow::pauseVideoIfPlaying: playWindow=" << static_cast<void*>(m_playWindow);
    if (m_playWindow && m_playWindow->isPlaying()) {
        m_playWindow->pauseForInterruption();
        return true;
    }
    return false;
}

bool VideoListWindow::pauseVideoForOcclusion()
{
    qDebug() << "VideoListWindow::pauseVideoForOcclusion: playWindow=" << static_cast<void*>(m_playWindow);
    if (m_playWindow && m_playWindow->isPlaying()) {
        m_playWindow->pauseIfPlaying();
        return true;
    }
    return false;
}

bool VideoListWindow::resumeVideoAfterInterruption()
{
    qDebug() << "VideoListWindow::resumeVideoAfterInterruption: playWindow=" << static_cast<void*>(m_playWindow)
             << " visible=" << (m_playWindow ? m_playWindow->isVisible() : false);
    if (m_playWindow) {
        if (!m_playWindow->isVisible()) {
            qDebug() << "VideoListWindow::resumeVideoAfterInterruption: showing playWindow";
            m_playWindow->show();
            m_playWindow->raise();
            m_playWindow->activateWindow();
        }
        qDebug() << "VideoListWindow::resumeVideoAfterInterruption: calling resumeAfterInterruption";
        m_playWindow->resumeAfterInterruption();
        return true;
    }
    qWarning() << "VideoListWindow::resumeVideoAfterInterruption: no playWindow to resume";
    return false;
}

QWidget *VideoListWindow::videoPlayWindow() const
{
    return m_playWindow;
}

QString VideoListWindow::currentPlayingVideoPath() const
{
    const auto *playWindow = qobject_cast<const VideoPlayWindow *>(m_playWindow);
    return playWindow ? playWindow->currentVideoPath() : QString();
}

void VideoListWindow::setBluetoothManager(BluetoothManager *manager) {
    m_bluetoothManager = manager;
    if (m_playWindow) {
        m_playWindow->setBluetoothManager(manager);
    }
}

void VideoListWindow::setMusicWindow(MusicPlayerWindow *musicWindow) {
    m_musicWindow = musicWindow;
}

void VideoListWindow::playVideoFiles(const QStringList &videoList, int currentIdx,
                                     const std::function<void()> &returnToList,
                                     VideoPlaybackOrigin origin)
{
    if (videoList.isEmpty() || currentIdx < 0 || currentIdx >= videoList.size()) {
        return;
    }

    m_returnToListHandler = returnToList;

    if (!m_playWindow) {
        m_playWindow = new VideoPlayWindow();
        if (m_bluetoothManager) {
            m_playWindow->setBluetoothManager(m_bluetoothManager);
        }
        connect(m_playWindow, &VideoPlayWindow::requestReturnToList, this, [this]() {
            if (m_returnToListHandler) {
                m_returnToListHandler();
            } else {
                this->show();
            }
        });
        connect(m_playWindow, &VideoPlayWindow::requestReturnToMain, this, [this]() {
            m_returnToListHandler = nullptr;
            if (m_playWindow) {
                m_playWindow->hide();
            }
            emit requestReturnToMain();
            this->hide();
        });
    }
    if (m_musicWindow) {
        m_musicWindow->stopIfPlaying();
    }
    m_playWindow->setVideoFiles(videoList, currentIdx, origin);
    this->hide();
    m_playWindow->show();
    m_playWindow->raise();
    m_playWindow->activateWindow();
}

static bool isUsbMountSubdirValid(const QString &sub)
{
    // 有效 USB 子目录必须对应真实块设备，避免拔盘后残留空目录被当成已插入
    const QString devNode = QStringLiteral("/dev/%1").arg(sub);
    const QString sysBlock = QStringLiteral("/sys/block/%1").arg(sub);
    return QFile::exists(devNode) && QDir(sysBlock).exists();
}

static QString findFirstUsbVideoDirectory()
{
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady())
            continue;
        const QString root = storage.rootPath();
        if (root.startsWith(kUsbMountPrefix))
            return root;
    }
    QDir d(kUsbMountDir);
    if (d.exists()) {
        const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs) {
            if (isUsbMountSubdirValid(sub))
                return kUsbMountPrefix + sub;
        }
    }
    return {};
}

void VideoListWindow::onItemClicked(QListWidgetItem *item) {
    QString itemPath = item->data(Qt::UserRole).toString();
    bool isDirectory = item->data(Qt::UserRole + 1).toBool();
    
    if (isDirectory) {
        // 打开文件夹
        loadVideoFiles(itemPath);
    } else {
        // 打开视频播放界面（替换当前界面，返回后再显示）
        qDebug() << "Opening video play window:" << itemPath;
        
        // 收集当前目录下所有视频文件作为播放列表
        QStringList videoList;
        int currentIdx = 0;
        QDir dir(m_currentPath);
        dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
        dir.setSorting(QDir::Name);
        QFileInfoList files = dir.entryInfoList();
        for (const QFileInfo &fi : files) {
            for (const QString &ext : m_videoExtensions) {
                if (fi.fileName().endsWith(ext.mid(1), Qt::CaseInsensitive)) {
                    if (fi.absoluteFilePath() == itemPath) {
                        currentIdx = videoList.size();
                    }
                    videoList << fi.absoluteFilePath();
                    break;
                }
            }
        }

        playVideoFiles(videoList, currentIdx);
    }
}
