#include "videolistwindow.h"
#include "videoplaywindow.h"
#include "devicedetect.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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

VideoListWindow::VideoListWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_homeButton(new QPushButton(this))
    , m_backDirButton(new QPushButton(this))
    , m_videoListWidget(new QListWidget(this))
    , m_pathLabel(new QLabel("USB > 电影 > 欧美", this))
    , m_timeLabel(new QLabel(QDateTime::currentDateTime().toString("hh:mm"), this))
    , m_currentPath(QDir::homePath())
    , m_initialPath(QDir::homePath())
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
    loadVideoFiles(m_currentPath);
    
    connect(m_homeButton, &QPushButton::clicked, this, &VideoListWindow::onHomeClicked);
    connect(m_backDirButton, &QPushButton::clicked, this, &VideoListWindow::onBackClicked);
    connect(m_videoListWidget, &QListWidget::itemClicked, this, &VideoListWindow::onItemClicked);
}

VideoListWindow::~VideoListWindow() {
}

void VideoListWindow::closeEvent(QCloseEvent *event) {
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void VideoListWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setStyleSheet(
        "background-image: url(:/images/inside_background.png); "
        "background-repeat: no-repeat; "
        "background-position: center;"
    );
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // ===== 顶部栏 - 使用与主窗口相同的设计 =====
    QWidget *topBar = new QWidget(this);
    topBar->setFixedHeight(82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");
    
    QGridLayout *topLayout = new QGridLayout(topBar);
    topLayout->setContentsMargins(16, 0, 16, 0);
    topLayout->setSpacing(0);
    topLayout->setColumnStretch(0, 1);
    topLayout->setColumnStretch(1, 0);
    topLayout->setColumnStretch(2, 1);
    
    // 左侧返回按钮
    QWidget *leftWidget = new QWidget();
    QHBoxLayout *leftLayout = new QHBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    m_homeButton->setFixedSize(48, 48);
    m_homeButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-image: url(:/images/pict_home_up.png); "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/pict_home_down.png); "
        "}"
    );
    leftLayout->addWidget(m_homeButton);
    leftLayout->addStretch();
    topLayout->addWidget(leftWidget, 0, 0);
    
    // 中间标题
    QLabel *titleLabel = new QLabel("视频播放", this);
    titleLabel->setStyleSheet("color: #fff; font-size: 36px; font-weight: bold; background: transparent;");
    titleLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(titleLabel, 0, 1);
    
    // 右侧图标区域
    QWidget *iconWidget = new QWidget(this);
    QHBoxLayout *iconLayout = new QHBoxLayout(iconWidget);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    iconLayout->setSpacing(16);
    
    // BT 图标
    QPushButton *btBtn = new QPushButton(this);
    btBtn->setFixedSize(48, 48);
    btBtn->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-image: url(:/images/pict_buetooth.png); "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/pict_buetooth_on.png); "
        "}"
    );
    iconLayout->addWidget(btBtn);
    
    // USB 图标
    QPushButton *usbBtn = new QPushButton(this);
    usbBtn->setFixedSize(48, 48);
    usbBtn->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-image: url(:/images/pict_usb.png); "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/pict_usb_on.png); "
        "}"
    );
    iconLayout->addWidget(usbBtn);
    
    // 音量
    QWidget *volWidget = new QWidget();
    QHBoxLayout *volLayout = new QHBoxLayout(volWidget);
    volLayout->setContentsMargins(0, 0, 0, 0);
    volLayout->setSpacing(6);
    
    QPushButton *volBtn = new QPushButton(this);
    volBtn->setFixedSize(48, 48);
    volBtn->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-image: url(:/images/pict_volume.png); "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/pict_volume_mute.png); "
        "}"
    );
    volLayout->addWidget(volBtn);
    
    QLabel *volumeLabel = new QLabel("10", this);
    volumeLabel->setStyleSheet("color: #fff; font-size: 18px;");
    volLayout->addWidget(volumeLabel);
    
    iconLayout->addWidget(volWidget);
    
    // 时间
    m_timeLabel->setStyleSheet("color: #fff; font-size: 18px;");
    m_timeLabel->setFixedWidth(100);
    iconLayout->addWidget(m_timeLabel);
    
    topLayout->addWidget(iconWidget, 0, 2, Qt::AlignRight);
    
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
    );
    m_videoListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_videoListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
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
    m_videoListWidget->setGridSize(QSize(188, 178));  // 水平188×5=940≤944, 垂直178×2=356=HTML高度
    m_videoListWidget->setFixedSize(944, 356);  // 精确匹配HTML ul高度356px
    
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
    m_pathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    
    listLayout->addWidget(m_pathLabel);
    
    mainLayout->addWidget(listWidget, 1);
}

void VideoListWindow::loadVideoFiles(const QString &directory) {
    m_currentPath = directory;
    m_videoListWidget->clear();
    
    QDir dir(directory);
    dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::DirsFirst);
    
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
    
    updatePath(directory);
    qDebug() << "Loaded" << m_videoListWidget->count() << "items in total";
}

void VideoListWindow::updatePath(const QString &path) {
    QString displayPath = path;
    if (displayPath == QDir::homePath()) {
        displayPath = "首页";
    } else {
        displayPath = displayPath.replace(QDir::homePath(), "~");
    }
    m_pathLabel->setText(displayPath);
}

void VideoListWindow::onHomeClicked() {
    close();
}

void VideoListWindow::onBackClicked() {
    if (m_currentPath != m_initialPath) {
        // 返回上一级目录
        QDir dir(m_currentPath);
        if (dir.cdUp()) {
            loadVideoFiles(dir.absolutePath());
        }
    } else {
        // 在初始目录，关闭窗口
        close();
    }
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
        
        VideoPlayWindow *playWindow = new VideoPlayWindow(this);
        playWindow->setAttribute(Qt::WA_DeleteOnClose);
        playWindow->setVideoFiles(videoList, currentIdx);
        connect(playWindow, &VideoPlayWindow::requestReturnToList, this, [this]() {
            this->show();
        });
        connect(playWindow, &QObject::destroyed, this, [this]() {
            this->show();
        });
        this->hide();
        playWindow->show();
    }
}
