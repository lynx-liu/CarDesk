#include "musicplayerwindow.h"
#include "devicedetect.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QScreen>
#include <QApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTime>

MusicPlayerWindow::MusicPlayerWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_titleLabel(new QLabel("音频播放", this))
    , m_nowPlayingLabel(new QLabel("未播放", this))
    , m_playlistWidget(new QListWidget(this))
    , m_homeButton(new QPushButton(this))
    , m_usbTab(new QPushButton(this))
    , m_btTab(new QPushButton(this))
    , m_listButton(new QPushButton(this))
    , m_prevButton(new QPushButton(this))
    , m_playButton(new QPushButton(this))
    , m_nextButton(new QPushButton(this))
    , m_loopButton(new QPushButton(this))
    , m_currentIndex(-1)
    , m_playerProcess(nullptr)
    , m_isUsbMode(true)
{
    setWindowTitle("音乐播放");
    setFixedSize(1280, 720);
    
    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else {
        if (QApplication::primaryScreen()) {
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
        }
    }
    
    setupUI();
    loadMusicFiles();
    
    connect(m_homeButton, &QPushButton::clicked, this, [this]() {
        close();
    });
    connect(m_listButton, &QPushButton::clicked, this, &MusicPlayerWindow::onListClicked);
    connect(m_prevButton, &QPushButton::clicked, this, &MusicPlayerWindow::onPreviousMusic);
    connect(m_playButton, &QPushButton::clicked, this, &MusicPlayerWindow::onPlayMusic);
    connect(m_nextButton, &QPushButton::clicked, this, &MusicPlayerWindow::onNextMusic);
    connect(m_loopButton, &QPushButton::clicked, this, [this]() {
        // 切换循环模式图标
        static bool isRandom = false;
        isRandom = !isRandom;
        if (isRandom) {
            m_loopButton->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/butt_music_random_up.png); background-repeat: no-repeat; background-position: center; } "
                "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_random_down.png); }"
            );
        } else {
            m_loopButton->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/butt_music_circle_up.png); background-repeat: no-repeat; background-position: center; } "
                "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_circle_down.png); }"
            );
        }
    });
    connect(m_usbTab, &QPushButton::clicked, this, &MusicPlayerWindow::onUsbTabClicked);
    connect(m_btTab, &QPushButton::clicked, this, &MusicPlayerWindow::onBtTabClicked);
}

MusicPlayerWindow::~MusicPlayerWindow() {
    if (m_playerProcess) {
        m_playerProcess->blockSignals(true);
        m_playerProcess->disconnect(this);
        if (m_playerProcess->state() != QProcess::NotRunning) {
            m_playerProcess->terminate();
            if (!m_playerProcess->waitForFinished(1500)) {
                m_playerProcess->kill();
                m_playerProcess->waitForFinished(1500);
            }
        }
        delete m_playerProcess;
        m_playerProcess = nullptr;
    }
}

void MusicPlayerWindow::closeEvent(QCloseEvent *event) {
    onStopMusic();
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void MusicPlayerWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    centralWidget->setStyleSheet("background-image: url(:/images/inside_background.png);");
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // ===== 顶部栏：和主界面统一 (HTML: .top) =====
    QWidget *topBar = new QWidget();
    topBar->setFixedSize(1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");
    
    // 返回主页按钮 (HTML: .top_icon_home)
    m_homeButton->setParent(topBar);
    m_homeButton->setGeometry(12, 12, 48, 48);
    m_homeButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_home_up.png); background-repeat: no-repeat; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/pict_home_down.png); }"
    );
    m_homeButton->setCursor(Qt::PointingHandCursor);
    
    // 标题 (HTML: .top h1)
    m_titleLabel->setParent(topBar);
    m_titleLabel->setGeometry(0, 0, 1280, 72);
    m_titleLabel->setText("音频播放");
    m_titleLabel->setStyleSheet("color: #fff; font-size: 36px; font-weight: 700; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);  // 让鼠标事件穿透到下层按钮
    
    // 右侧状态图标区域 (HTML: .top .top_icon)
    QWidget *topIconWidget = new QWidget(topBar);
    topIconWidget->setGeometry(1280 - 16 - 280, 12, 280, 48);
    topIconWidget->setStyleSheet("background: transparent;");
    
    QHBoxLayout *topIconLayout = new QHBoxLayout(topIconWidget);
    topIconLayout->setContentsMargins(0, 0, 0, 0);
    topIconLayout->setSpacing(16);
    
    // 蓝牙图标
    QLabel *btIcon = new QLabel();
    btIcon->setFixedSize(48, 48);
    btIcon->setPixmap(QPixmap(":/images/pict_buetooth.png"));
    topIconLayout->addWidget(btIcon);
    
    // USB图标
    QLabel *usbIcon = new QLabel();
    usbIcon->setFixedSize(48, 48);
    usbIcon->setPixmap(QPixmap(":/images/pict_usb.png"));
    topIconLayout->addWidget(usbIcon);
    
    // 音量
    QLabel *volIcon = new QLabel();
    volIcon->setFixedSize(48, 48);
    volIcon->setPixmap(QPixmap(":/images/pict_volume.png"));
    QLabel *volLabel = new QLabel("10");
    volLabel->setStyleSheet("color: #fff; font-size: 36px;");
    topIconLayout->addWidget(volIcon);
    topIconLayout->addWidget(volLabel);
    
    // 时间
    QLabel *timeIcon = new QLabel(QTime::currentTime().toString("hh:mm"));
    timeIcon->setStyleSheet("color: #fff; font-size: 36px;");
    topIconLayout->addWidget(timeIcon);
    
    mainLayout->addWidget(topBar);
    
    // ===== Tab标签栏 (HTML: .tab) =====
    QWidget *tabWidget = new QWidget();
    tabWidget->setFixedHeight(66);
    tabWidget->setStyleSheet("background: transparent;");
    
    QHBoxLayout *tabLayout = new QHBoxLayout(tabWidget);
    tabLayout->setContentsMargins(480, 0, 0, 0);
    tabLayout->setSpacing(0);
    
    m_usbTab->setText("USB音乐");
    m_usbTab->setFixedSize(160, 66);
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_on.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_on.png); "
        "color: #fff; font-size: 28px; }"
    );
    m_usbTab->setCursor(Qt::PointingHandCursor);
    
    m_btTab->setText("蓝牙音乐");
    m_btTab->setFixedSize(160, 66);
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_down.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_down.png); "
        "color: #fff; font-size: 28px; }"
    );
    m_btTab->setCursor(Qt::PointingHandCursor);
    
    tabLayout->addWidget(m_usbTab);
    tabLayout->addWidget(m_btTab);
    tabLayout->addStretch();
    
    mainLayout->addWidget(tabWidget);
    
    // ===== 音乐内容区 (HTML: .music_con width:1040px margin:16px auto) =====
    QWidget *musicCon = new QWidget(centralWidget);
    musicCon->setFixedWidth(1040);
    musicCon->setStyleSheet("background: transparent;");
    
    QVBoxLayout *conLayout = new QVBoxLayout(musicCon);
    conLayout->setContentsMargins(0, 16, 0, 0);
    conLayout->setSpacing(0);
    
    // === 音乐详情 (HTML: .music_detail display:flex) ===
    QWidget *detailWidget = new QWidget(musicCon);
    QHBoxLayout *detailLayout = new QHBoxLayout(detailWidget);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    
    // 专辑图 (HTML: .music_img width:210px)
    QLabel *albumImg = new QLabel();
    albumImg->setFixedSize(210, 210);
    albumImg->setPixmap(QPixmap(":/images/music_show.png"));
    detailLayout->addWidget(albumImg);
    
    // 歌曲信息 (HTML: .music_mes flex:1 padding-left:80px)
    QWidget *mesWidget = new QWidget(detailWidget);
    mesWidget->setStyleSheet("background: transparent;");
    QVBoxLayout *mesLayout = new QVBoxLayout(mesWidget);
    mesLayout->setContentsMargins(80, 0, 0, 0);
    mesLayout->setSpacing(0);
    
    // 歌曲名 (HTML: .music_mes p font-size:48px line-height:72px)
    m_nowPlayingLabel->setText("Different Word");
    m_nowPlayingLabel->setStyleSheet(
        "color: #fff; font-size: 48px; background: transparent;"
    );
    m_nowPlayingLabel->setFixedHeight(76);
    mesLayout->addWidget(m_nowPlayingLabel);
    
    // 歌手 (HTML: .music_mes li background singer_icon padding-left:56px)
    QLabel *singerLabel = new QLabel("Alan Walker");
    singerLabel->setFixedHeight(64);  // 40px + 24px margin-top
    singerLabel->setStyleSheet(
        "color: #fff; font-size: 32px; line-height: 40px; padding-left: 56px; margin-top: 24px; "
        "background: url(:/images/pict_music_singer_icon.png) no-repeat left center;"
    );
    mesLayout->addWidget(singerLabel);
    
    // 专辑 (HTML: .music_mes li:last-child background album_icon)
    QLabel *albumLabel = new QLabel("Faded");
    albumLabel->setFixedHeight(64);  // 40px + 24px margin-top
    albumLabel->setStyleSheet(
        "color: #fff; font-size: 32px; line-height: 40px; padding-left: 56px; margin-top: 24px; "
        "background: url(:/images/pict_music_album_icon.png) no-repeat left center;"
    );
    mesLayout->addWidget(albumLabel);
    mesLayout->addStretch();
    
    detailLayout->addWidget(mesWidget, 1);
    
    // 功能按钮 (HTML: .music_func width:80px)
    QWidget *funcWidget = new QWidget(detailWidget);
    funcWidget->setFixedWidth(80);
    QVBoxLayout *funcLayout = new QVBoxLayout(funcWidget);
    funcLayout->setContentsMargins(0, 8, 0, 8);
    funcLayout->setSpacing(0);
    
    QPushButton *collectBtn = new QPushButton();
    collectBtn->setFixedSize(60, 60);
    collectBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_collection_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_collection_down.png); }"
    );
    collectBtn->setCursor(Qt::PointingHandCursor);
    
    QPushButton *scanBtn = new QPushButton();
    scanBtn->setFixedSize(60, 60);
    scanBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_scan_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_scan_down.png); }"
    );
    scanBtn->setCursor(Qt::PointingHandCursor);
    
    funcLayout->addWidget(collectBtn);
    funcLayout->addStretch();
    funcLayout->addWidget(scanBtn);
    
    detailLayout->addWidget(funcWidget);
    conLayout->addWidget(detailWidget);
    
    // === 音乐控制 (HTML: .music_control margin-top:16px) ===
    QWidget *controlWidget = new QWidget(musicCon);
    QVBoxLayout *controlLayout = new QVBoxLayout(controlWidget);
    controlLayout->setContentsMargins(0, 16, 0, 0);
    controlLayout->setSpacing(0);
    
    // 进度条 (HTML: .music_pro display:flex)
    QWidget *proWidget = new QWidget(controlWidget);
    QHBoxLayout *proLayout = new QHBoxLayout(proWidget);
    proLayout->setContentsMargins(0, 0, 0, 0);
    proLayout->setSpacing(0);
    
    QLabel *timeLabel = new QLabel("03:20");
    timeLabel->setStyleSheet("color: #fff; font-size: 24px; line-height: 36px;");
    
    QLabel *progImg = new QLabel();
    progImg->setFixedHeight(8);
    progImg->setStyleSheet("padding: 14px 24px 0;");
    progImg->setPixmap(QPixmap(":/images/prog_music.png"));
    
    QLabel *durationLabel = new QLabel("04:48");
    durationLabel->setStyleSheet("color: #fff; font-size: 24px; line-height: 36px;");
    
    proLayout->addWidget(timeLabel);
    proLayout->addWidget(progImg, 1);
    proLayout->addWidget(durationLabel);
    controlLayout->addWidget(proWidget);
    
    // 控制按钮 (HTML: .music_btn ul display:flex justify-content:space-around margin-top:10px)
    QWidget *btnWidget = new QWidget(controlWidget);
    btnWidget->setStyleSheet("background: transparent;");
    btnWidget->raise();  // 确保在上层
    QHBoxLayout *btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(0, 22, 0, 0);
    btnLayout->setSpacing(0);
    
    // 列表按钮 60x60
    m_listButton->setFixedSize(60, 60);
    m_listButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_list_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_list_down.png); }"
    );
    m_listButton->setCursor(Qt::PointingHandCursor);
    
    // 上一首 60x60
    m_prevButton->setFixedSize(60, 60);
    m_prevButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_prev_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_prev_down.png); }"
    );
    m_prevButton->setCursor(Qt::PointingHandCursor);
    
    // 播放 84x84
    m_playButton->setFixedSize(84, 84);
    m_playButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_play_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_play_down.png); }"
    );
    m_playButton->setCursor(Qt::PointingHandCursor);
    
    // 下一首 60x60
    m_nextButton->setFixedSize(60, 60);
    m_nextButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_next_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_next_down.png); }"
    );
    m_nextButton->setCursor(Qt::PointingHandCursor);
    
    // 循环模式 60x60
    m_loopButton->setFixedSize(60, 60);
    m_loopButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_circle_up.png); background-repeat: no-repeat; background-position: center; } "
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_circle_down.png); }"
    );
    m_loopButton->setCursor(Qt::PointingHandCursor);
    
    btnLayout->addStretch();
    btnLayout->addWidget(m_listButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_prevButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_playButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_nextButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_loopButton);
    btnLayout->addStretch();
    
    controlLayout->addWidget(btnWidget);
    conLayout->addWidget(controlWidget);
    
    // 播放列表 (HTML: .music_play_list)
    m_playlistWidget->setFixedHeight(120);
    m_playlistWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; } "
        "QListWidget::item { width: 120px; height: 120px; margin: 0 12px; }"
    );
    conLayout->addWidget(m_playlistWidget);
    
    // 居中music_con
    QHBoxLayout *centerLayout = new QHBoxLayout();
    centerLayout->addStretch();
    centerLayout->addWidget(musicCon);
    centerLayout->addStretch();
    mainLayout->addLayout(centerLayout);
}

void MusicPlayerWindow::loadMusicFiles() {
    m_musicFiles.clear();
    m_playlistWidget->clear();
    scanMusicDirectories();
    
    for (const QString &file : m_musicFiles) {
        QFileInfo fileInfo(file);
        QListWidgetItem *item = new QListWidgetItem(m_playlistWidget);
        item->setText(fileInfo.fileName());
        m_playlistWidget->addItem(item);
    }
    
    qDebug() << "Loaded" << m_musicFiles.count() << "music files";
}

void MusicPlayerWindow::scanMusicDirectories() {
    QStringList searchDirs;
    
    searchDirs << QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    searchDirs << QDir::homePath() + "/Music";
    searchDirs << QDir::homePath() + "/music";
    searchDirs << QDir::homePath() + "/Downloads";
    searchDirs << "/tmp";
    searchDirs << "/media";
    searchDirs << "/mnt";
    
    QStringList musicExtensions = {"*.mp3", "*.flac", "*.wav", "*.aac", "*.ogg", "*.wma", "*.opus", "*.m4a"};
    
    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        
        dir.setFilter(QDir::Files | QDir::NoSymLinks);
        dir.setNameFilters(musicExtensions);
        
        QFileInfoList fileInfos = dir.entryInfoList();
        for (const QFileInfo &fileInfo : fileInfos) {
            if (!m_musicFiles.contains(fileInfo.absoluteFilePath())) {
                m_musicFiles << fileInfo.absoluteFilePath();
            }
        }
    }
}

void MusicPlayerWindow::onPlayMusic() {
    if (m_currentIndex < 0 || m_currentIndex >= m_musicFiles.count()) {
        m_currentIndex = 0;
    }
    
    if (m_currentIndex >= m_musicFiles.count()) {
        return;
    }
    
    QString musicPath = m_musicFiles[m_currentIndex];
    qDebug() << "Playing music:" << musicPath;
    updateNowPlaying();
    
    if (!m_playerProcess) {
        m_playerProcess = new QProcess(this);
        connect(m_playerProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &MusicPlayerWindow::onProcessFinished);
    }
    
    QStringList players = {"audacious", "rhythmbox", "cmus", "mpv", "ffplay"};
    
    for (const QString &player : players) {
        if (m_playerProcess->state() == QProcess::NotRunning) {
            m_playerProcess->start(player, QStringList() << musicPath);
            if (m_playerProcess->waitForStarted()) {
                qDebug() << "Music player started:" << player;
                return;
            }
        }
    }
}

void MusicPlayerWindow::onStopMusic() {
    if (m_playerProcess && m_playerProcess->state() == QProcess::Running) {
        m_playerProcess->terminate();
        if (!m_playerProcess->waitForFinished(1500)) {
            m_playerProcess->kill();
            m_playerProcess->waitForFinished(1500);
        }
    }
}

void MusicPlayerWindow::onNextMusic() {
    if (m_currentIndex < m_musicFiles.count() - 1) {
        m_currentIndex++;
        onPlayMusic();
    }
}

void MusicPlayerWindow::onPreviousMusic() {
    if (m_currentIndex > 0) {
        m_currentIndex--;
        onPlayMusic();
    }
}

void MusicPlayerWindow::onListClicked() {
    // 播放列表点击处理
}

void MusicPlayerWindow::onUsbTabClicked() {
    if (m_isUsbMode) {
        return;
    }
    m_isUsbMode = true;
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_on.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_on.png); "
        "color: #fff; font-size: 28px; }"
    );
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_down.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_down.png); "
        "color: #fff; font-size: 28px; }"
    );
    loadMusicFiles();
}

void MusicPlayerWindow::onBtTabClicked() {
    if (!m_isUsbMode) {
        return;
    }
    m_isUsbMode = false;
    onStopMusic();
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_down.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_down.png); "
        "color: #fff; font-size: 28px; }"
    );
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_on.png); "
        "color: #fff; font-size: 28px; } "
        "QPushButton:hover, QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_on.png); "
        "color: #fff; font-size: 28px; }"
    );
    m_musicFiles.clear();
    m_playlistWidget->clear();
    m_nowPlayingLabel->setText("蓝牙音乐");
}

void MusicPlayerWindow::updateNowPlaying() {
    if (m_currentIndex >= 0 && m_currentIndex < m_musicFiles.count()) {
        QFileInfo fileInfo(m_musicFiles[m_currentIndex]);
        m_nowPlayingLabel->setText(fileInfo.baseName());
    }
}

void MusicPlayerWindow::onGoBack() {
    close();
}

void MusicPlayerWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitStatus);
    qDebug() << "Music player finished with exit code:" << exitCode;
}
