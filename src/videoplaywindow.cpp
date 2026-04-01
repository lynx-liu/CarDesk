#include "videoplaywindow.h"
#include "devicedetect.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QScreen>
#include <QApplication>
#include <QFileInfo>
#include <QProcess>
#include <QMouseEvent>
#include <QTimer>

VideoPlayWindow::VideoPlayWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_titleLabel(new QLabel("速度与激情", this))
    , m_prevButton(new QPushButton(this))
    , m_playButton(new QPushButton(this))
    , m_nextButton(new QPushButton(this))
    , m_backButton(new QPushButton(this))
    , m_progressSlider(new QSlider(Qt::Horizontal, this))
    , m_timeLabel(new QLabel("01:24:20", this))
    , m_durationLabel(new QLabel("01:44:48", this))
    , m_currentIndex(-1)
    , m_playerProcess(nullptr)
    , m_hideTimer(nullptr)
    , m_mediaPlayer(nullptr)
    , m_videoWidget(nullptr)
{
    setWindowTitle("视频播放");
    setFixedSize(1280, 720);
    
    // 初始化媒体播放器
    m_mediaPlayer = new QMediaPlayer(this);
    m_videoWidget = new QVideoWidget(this);
    m_mediaPlayer->setVideoOutput(m_videoWidget);
    
    // 连接媒体播放器信号
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &VideoPlayWindow::onMediaStatusChanged);
    connect(m_mediaPlayer, &QMediaPlayer::stateChanged, this, &VideoPlayWindow::onPlaybackStateChanged);
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoPlayWindow::onPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this, &VideoPlayWindow::onDurationChanged);
    
    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else {
        if (QApplication::primaryScreen()) {
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
        }
    }
    
    setupUI();
    loadVideoFiles();
    
    connect(m_backButton, &QPushButton::clicked, this, [this]() {
        emit requestReturnToList();
        close();
    });
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
        if (m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
            m_mediaPlayer->pause();
        } else {
            if (m_mediaPlayer->media().isNull()) {
                onPlayVideo();
            } else {
                m_mediaPlayer->play();
            }
        }
    });
    connect(m_prevButton, &QPushButton::clicked, this, &VideoPlayWindow::onPreviousVideo);
    connect(m_nextButton, &QPushButton::clicked, this, &VideoPlayWindow::onNextVideo);
    
    // 连接进度条：拖动时跳转播放位置
    connect(m_progressSlider, &QSlider::sliderPressed, this, [this]() {
        // 开始拖动时暂时断开位置更新，避免冲突
        disconnect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoPlayWindow::onPositionChanged);
    });
    connect(m_progressSlider, &QSlider::sliderReleased, this, [this]() {
        // 释放时跳转到指定位置
        if (m_mediaPlayer->duration() > 0) {
            qint64 position = (m_progressSlider->value() * m_mediaPlayer->duration()) / 1000;
            m_mediaPlayer->setPosition(position);
        }
        // 重新连接位置更新
        connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoPlayWindow::onPositionChanged);
    });
    
    m_hideTimer = new QTimer(this);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        m_hideTimer->stop();
    });
}

VideoPlayWindow::~VideoPlayWindow() {
    // 先停止媒体播放器
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
        m_mediaPlayer->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
        disconnect(m_mediaPlayer, nullptr, this, nullptr);
    }
    
    if (m_playerProcess) {
        m_playerProcess->terminate();
        m_playerProcess->waitForFinished();
        delete m_playerProcess;
    }
}

void VideoPlayWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setContentsMargins(0, 0, 0, 0);
    setCentralWidget(centralWidget);
    
    // 视频显示区域（全屏填充）
    m_videoWidget->setParent(centralWidget);
    m_videoWidget->setGeometry(0, 0, 1280, 720);
    m_videoWidget->setStyleSheet("background: black;");
    
    // ===== 顶部栏：绝对定位 =====
    QWidget *topBar = new QWidget(centralWidget);
    topBar->setGeometry(0, 0, 1280, 72);
    topBar->setStyleSheet("background: rgba(0, 0, 0, 0.5);");
    topBar->raise();  // 确保在视频上层

    // 返回按钮（HTML: .video_play_top span）
    m_backButton->setParent(topBar);
    m_backButton->setFixedSize(48, 48);
    m_backButton->move(12, 12);
    m_backButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-color: transparent; "
        "  background-image: url(:/images/butt_video_back_up.png); "
        "  background-repeat: no-repeat; "
        "  background-position: center; "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/butt_video_back_down.png); "
        "}"
    );

    // 标题（HTML: .video_play_top h1）
    m_titleLabel->setParent(topBar);
    m_titleLabel->setGeometry(100, 0, 1080, 72);  // 留出左右空间，避免遮挡按钮
    m_titleLabel->setStyleSheet(
        "color: #fff; "
        "font-size: 36px; "
        "font-weight: 700; "
        "background: transparent;"
    );
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);  // 让鼠标事件穿透到下层
    
    // ===== 底部栏：绝对定位 =====
    QWidget *bottomBar = new QWidget(centralWidget);
    bottomBar->setGeometry(0, 720 - 132, 1280, 132);
    bottomBar->setStyleSheet("background: rgba(0, 0, 0, 0.5);");
    bottomBar->raise();  // 确保在视频上层

    // HTML: padding 24px 48px, display:flex
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(48, 24, 48, 24);
    bottomLayout->setSpacing(0);

    // 左侧按钮区域（HTML: .video_play_btn width 324）
    QWidget *buttonWidget = new QWidget();
    buttonWidget->setFixedWidth(324);
    buttonWidget->setStyleSheet("background: transparent;");  // 设置透明背景
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(0);

    // 上一首
    m_prevButton->setFixedSize(60, 60);
    m_prevButton->setIcon(QIcon(":/images/butt_music_prev_up.png"));
    m_prevButton->setIconSize(QSize(60, 60));
    m_prevButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "}"
    );

    // 播放/暂停
    m_playButton->setFixedSize(84, 84);
    m_playButton->setIcon(QIcon(":/images/butt_music_play_up.png"));
    m_playButton->setIconSize(QSize(84, 84));
    m_playButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "}"
    );

    // 下一首
    m_nextButton->setFixedSize(60, 60);
    m_nextButton->setIcon(QIcon(":/images/butt_music_next_up.png"));
    m_nextButton->setIconSize(QSize(60, 60));
    m_nextButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "}"
    );

    buttonLayout->addWidget(m_prevButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_nextButton);

    // 右侧控制区域（HTML: .video_play_control）
    QWidget *controlWidget = new QWidget();
    QHBoxLayout *controlLayout = new QHBoxLayout(controlWidget);
    controlLayout->setContentsMargins(48, 0, 0, 0);
    controlLayout->setSpacing(0);

    m_timeLabel->setStyleSheet("color: #fff; font-size: 24px;");
    m_timeLabel->setFixedWidth(100);
    m_timeLabel->setAlignment(Qt::AlignCenter);

    QWidget *progressContainer = new QWidget();
    QHBoxLayout *progressLayout = new QHBoxLayout(progressContainer);
    progressLayout->setContentsMargins(24, 14, 24, 14);
    progressLayout->setSpacing(0);

    // 使用真正的滑块作为进度条
    m_progressSlider->setFixedHeight(8);
    m_progressSlider->setMinimum(0);
    m_progressSlider->setMaximum(1000);
    m_progressSlider->setValue(0);
    m_progressSlider->setStyleSheet(
        "QSlider::groove:horizontal { "
        "  background: rgba(255, 255, 255, 0.3); "
        "  height: 8px; "
        "  border-radius: 4px; "
        "} "
        "QSlider::handle:horizontal { "
        "  background: #fff; "
        "  width: 16px; "
        "  height: 16px; "
        "  border-radius: 8px; "
        "  margin: -4px 0; "
        "} "
        "QSlider::sub-page:horizontal { "
        "  background: #00a0e9; "
        "  border-radius: 4px; "
        "}"
    );

    progressLayout->addWidget(m_progressSlider);

    m_durationLabel->setStyleSheet("color: #fff; font-size: 24px;");
    m_durationLabel->setFixedWidth(100);
    m_durationLabel->setAlignment(Qt::AlignCenter);

    controlLayout->addWidget(m_timeLabel);
    controlLayout->addWidget(progressContainer);
    controlLayout->addWidget(m_durationLabel);

    bottomLayout->addWidget(buttonWidget);
    bottomLayout->addWidget(controlWidget, 1);
}

void VideoPlayWindow::loadVideoFiles() {
    m_videoFiles.clear();
    scanVideoDirectories();
    qDebug() << "Loaded" << m_videoFiles.count() << "video files";
}

void VideoPlayWindow::scanVideoDirectories() {
    QStringList searchDirs;
    
    searchDirs << QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    searchDirs << QDir::homePath() + "/Videos";
    searchDirs << QDir::homePath() + "/videos";
    searchDirs << QDir::homePath() + "/Downloads";
    searchDirs << "/tmp";
    searchDirs << "/media";
    searchDirs << "/mnt";
    
    QStringList videoExtensions = {"*.mp4", "*.mkv", "*.avi", "*.mov", "*.flv", "*.wmv", "*.webm", "*.m4v"};
    
    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        
        dir.setFilter(QDir::Files | QDir::NoSymLinks);
        dir.setNameFilters(videoExtensions);
        
        QFileInfoList fileInfos = dir.entryInfoList();
        for (const QFileInfo &fileInfo : fileInfos) {
            if (!m_videoFiles.contains(fileInfo.absoluteFilePath())) {
                m_videoFiles << fileInfo.absoluteFilePath();
            }
        }
    }
}

void VideoPlayWindow::onPlayVideo() {
    if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        m_currentIndex = 0;
    }
    
    if (m_currentIndex >= m_videoFiles.count()) {
        return;
    }
    
    QString videoPath = m_videoFiles[m_currentIndex];
    qDebug() << "Playing video:" << videoPath;
    updateTitle();
    
    // 使用QMediaPlayer播放视频
    m_mediaPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(videoPath)));
    m_mediaPlayer->play();
}

void VideoPlayWindow::onNextVideo() {
    if (m_currentIndex < m_videoFiles.count() - 1) {
        m_currentIndex++;
        onPlayVideo();
    }
}

void VideoPlayWindow::onPreviousVideo() {
    if (m_currentIndex > 0) {
        m_currentIndex--;
        onPlayVideo();
    }
}

void VideoPlayWindow::updateTitle() {
    if (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count()) {
        QFileInfo fileInfo(m_videoFiles[m_currentIndex]);
        m_titleLabel->setText(fileInfo.baseName());
    }
}

void VideoPlayWindow::setVideoFiles(const QStringList &files, int currentIndex) {
    m_videoFiles = files;
    m_currentIndex = currentIndex;
    if (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count()) {
        updateTitle();
        // 立即播放视频
        onPlayVideo();
    }
}

void VideoPlayWindow::setCurrentVideo(const QString &filePath) {
    int idx = m_videoFiles.indexOf(filePath);
    if (idx >= 0) {
        m_currentIndex = idx;
    } else {
        m_videoFiles.prepend(filePath);
        m_currentIndex = 0;
    }
    updateTitle();
}

void VideoPlayWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitStatus);
    qDebug() << "Video player finished with exit code:" << exitCode;
}

void VideoPlayWindow::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    qDebug() << "Media status changed:" << status;
    if (status == QMediaPlayer::EndOfMedia) {
        // 播放完成，自动播放下一个
        onNextVideo();
    }
}

void VideoPlayWindow::onPlaybackStateChanged(QMediaPlayer::State state) {
    qDebug() << "Playback state changed:" << state;
    
    // 先清空图标，避免透明区域显示旧图标
    m_playButton->setIcon(QIcon());
    m_playButton->repaint();
    
    // 更新播放按钮图标
    if (state == QMediaPlayer::PlayingState) {
        m_playButton->setIcon(QIcon(":/images/butt_music_stop_up.png"));
    } else {
        m_playButton->setIcon(QIcon(":/images/butt_music_play_up.png"));
    }
}

void VideoPlayWindow::onPositionChanged(qint64 position) {
    // 更新时间显示（毫秒转为 HH:MM:SS 格式）
    int seconds = position / 1000;
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    m_timeLabel->setText(QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));
    
    // 更新进度条位置（不在拖动时）
    if (!m_progressSlider->isSliderDown() && m_mediaPlayer->duration() > 0) {
        int sliderValue = (position * 1000) / m_mediaPlayer->duration();
        m_progressSlider->setValue(sliderValue);
    }
}

void VideoPlayWindow::onDurationChanged(qint64 duration) {
    // 更新总时长显示
    int seconds = duration / 1000;
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    m_durationLabel->setText(QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));
}
