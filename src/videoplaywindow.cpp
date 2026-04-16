#include "videoplaywindow.h"
#include "devicedetect.h"
#include "appsignals.h"

#include <QVBoxLayout>
#include <QKeyEvent>
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
#include <QEvent>
#include <QTimer>

#ifdef CAR_DESK_USE_T507_SDK
LayerCtrl* LayerCreate_DE();
int set_output(void *param);

namespace {
typedef struct __screen_param
{
    int enable;
    int screen;
    int screenW;
    int screenH;
    int type;
    int mode;
} sc_param;

int sdkPlayerNotify(void *pUser, int msg, int ext1, void *para)
{
    VideoPlayWindow *window = static_cast<VideoPlayWindow*>(pUser);
    if (!window) {
        return 0;
    }

    switch (msg) {
    case AWPLAYER_MEDIA_INFO:
        if (ext1 == AW_MEDIA_INFO_RENDERING_START) {
            qDebug() << "XPlayer rendering started";
        }
        break;
    case AWPLAYER_MEDIA_PREPARED:
        qDebug() << "XPlayer prepared";
        break;
    case AWPLAYER_MEDIA_PLAYBACK_COMPLETE:
        qDebug() << "XPlayer playback complete";
        QMetaObject::invokeMethod(window, "onSdkPlaybackComplete", Qt::QueuedConnection);
        break;
    case AWPLAYER_MEDIA_SEEK_COMPLETE:
        qDebug() << "XPlayer seek complete";
        QMetaObject::invokeMethod(window, "onSdkSeekComplete", Qt::QueuedConnection);
        break;
    case AWPLAYER_MEDIA_SET_VIDEO_SIZE:
        if (para) {
            int *size = static_cast<int*>(para);
            qDebug() << "XPlayer video size:" << size[0] << "x" << size[1];
        }
        break;
    case AWPLAYER_MEDIA_ERROR:
        qDebug() << "XPlayer media error";
        break;
    default:
        break;
    }

    return 0;
}
} // namespace

// 全局 SDK 资源（进程生命周期）：只创建一次，复用于所有视频播放。
// 避免多次 LayerCreate_DE + XPlayerDestroy 导致 HwDisplay 内部状态累积腐蚀崩溃。
static XPlayer   *g_sdkPlayer    = nullptr;
static LayerCtrl *g_sdkLayerCtrl = nullptr;
static SoundCtrl *g_sdkSoundCtrl = nullptr;

static bool ensureSdkResourcesCreated()
{
    if (g_sdkPlayer) return true;  // 已初始化

    g_sdkLayerCtrl = LayerCreate_DE();
    g_sdkSoundCtrl = SoundDeviceCreate();
    if (!g_sdkLayerCtrl || !g_sdkSoundCtrl) {
        qWarning() << "SdkResource: create sinks failed";
        return false;
    }

    g_sdkPlayer = XPlayerCreate();
    if (!g_sdkPlayer) {
        qWarning() << "SdkResource: XPlayerCreate failed";
        return false;
    }

    if (XPlayerInitCheck(g_sdkPlayer) != 0) {
        qWarning() << "SdkResource: XPlayerInitCheck failed";
        return false;
    }

    XPlayerSetAudioSink(g_sdkPlayer, g_sdkSoundCtrl);
    XPlayerSetVideoSurfaceTexture(g_sdkPlayer, g_sdkLayerCtrl);

    sc_param screenParam;
    memset(&screenParam, 0, sizeof(screenParam));
    screenParam.enable = 1;
    screenParam.screen = 0;
    screenParam.screenW = DeviceDetect::instance().getScreenWidth();
    screenParam.screenH = DeviceDetect::instance().getScreenHeight();
    screenParam.type = 1;
    screenParam.mode = 4;
    set_output(&screenParam);

    return true;
}
#endif

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
    , m_topBar(nullptr)
    , m_bottomBar(nullptr)
    , m_useSdkPlayer(false)
    , m_controlsHidden(false)
    , m_sliderDragging(false)
    , m_wasPlayingBeforeSeek(false)
    , m_pausedForHome(false)
    , m_resumePositionMs(0)
#ifdef CAR_DESK_USE_T507_SDK
    , m_sdkPlayer(nullptr)
    , m_sdkSoundCtrl(nullptr)
    , m_sdkLayerCtrl(nullptr)
    , m_sdkSubCtrl(nullptr)
    , m_sdkDi(nullptr)
    , m_sdkTimer(nullptr)
    , m_sdkDurationMs(0)
    , m_sdkPlaying(false)
    , m_sdkSwitching(false)
    , m_sdkSeeking(false)
    , m_pendingRelease(false)
#endif
{
    setWindowTitle("视频播放");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
#ifdef CAR_DESK_USE_T507_SDK
    m_useSdkPlayer = (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT);
    if (m_useSdkPlayer) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAutoFillBackground(false);
        m_sdkTimer = new QTimer(this);
        m_sdkTimer->setInterval(500);
        connect(m_sdkTimer, &QTimer::timeout, this, &VideoPlayWindow::onSdkTick);
    }
#endif

    m_videoWidget = new QVideoWidget(this);

    // 初始化媒体播放器（仅PC使用，T507走XPlayer后端）
    if (!m_useSdkPlayer) {
        m_mediaPlayer = new QMediaPlayer(this);
        m_mediaPlayer->setVideoOutput(m_videoWidget);

        connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &VideoPlayWindow::onMediaStatusChanged);
        connect(m_mediaPlayer, &QMediaPlayer::stateChanged, this, &VideoPlayWindow::onPlaybackStateChanged);
        connect(m_mediaPlayer, &QMediaPlayer::positionChanged, this, &VideoPlayWindow::onPositionChanged);
        connect(m_mediaPlayer, &QMediaPlayer::durationChanged, this, &VideoPlayWindow::onDurationChanged);
    }

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
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            releaseSdkPlayer();
        }
#endif
        emit requestReturnToList();
        hide();
    });
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            if (!m_sdkPlayer) {
                onPlayVideo();
                return;
            }

            if (m_sdkPlaying) {
                XPlayerPause(m_sdkPlayer);
                m_sdkPlaying = false;
                setPlayButtonState(false);
            } else {
                XPlayerStart(m_sdkPlayer);
                m_sdkPlaying = true;
                setPlayButtonState(true);
                if (m_sdkTimer && !m_sdkTimer->isActive()) {
                    m_sdkTimer->start();
                }
            }
            return;
        }
#endif
        if (!m_mediaPlayer) {
            return;
        }
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
        m_sliderDragging = true;
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            m_wasPlayingBeforeSeek = m_sdkPlaying;
            if (m_sdkPlaying) {
                XPlayerPause(m_sdkPlayer);
                m_sdkPlaying = false;
                setPlayButtonState(false);
            }
            return;
        }
#endif
        if (!m_mediaPlayer) {
            return;
        }
        m_wasPlayingBeforeSeek = (m_mediaPlayer->state() == QMediaPlayer::PlayingState);
        if (m_wasPlayingBeforeSeek) {
            m_mediaPlayer->pause();
        }
    });
    connect(m_progressSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (!m_sliderDragging) {
            return;
        }
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            if (m_sdkDurationMs > 0) {
                qint64 positionMs = (static_cast<qint64>(value) * m_sdkDurationMs) / 1000;
                updateTimeAndSlider(positionMs, m_sdkDurationMs);
            }
            return;
        }
#endif
        if (!m_mediaPlayer) {
            return;
        }
        if (m_mediaPlayer->duration() > 0) {
            qint64 positionMs = (static_cast<qint64>(value) * m_mediaPlayer->duration()) / 1000;
            updateTimeAndSlider(positionMs, m_mediaPlayer->duration());
        }
    });
    connect(m_progressSlider, &QSlider::sliderReleased, this, [this]() {
        if (!m_sliderDragging) {
            return;
        }
        m_sliderDragging = false;
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            if (m_sdkPlayer && m_sdkDurationMs > 0) {
                const qint64 positionMs = (m_progressSlider->value() * m_sdkDurationMs) / 1000;
                XPlayerSeekTo(m_sdkPlayer, static_cast<int>(positionMs), AW_SEEK_CLOSEST_SYNC);
                if (m_wasPlayingBeforeSeek) {
                    XPlayerStart(m_sdkPlayer);
                    m_sdkPlaying = true;
                    setPlayButtonState(true);
                    if (m_sdkTimer && !m_sdkTimer->isActive()) {
                        m_sdkTimer->start();
                    }
                }
            }
            return;
        }
#endif
        // 释放时跳转到指定位置
        if (!m_mediaPlayer) {
            return;
        }
        if (m_mediaPlayer->duration() > 0) {
            qint64 position = (m_progressSlider->value() * m_mediaPlayer->duration()) / 1000;
            m_mediaPlayer->setPosition(position);
        }
        if (m_wasPlayingBeforeSeek && m_mediaPlayer->media().isNull() == false) {
            m_mediaPlayer->play();
        }
    });
    
    m_hideTimer = new QTimer(this);
    m_hideTimer->setInterval(5000);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        hideControls();
    });
    qApp->installEventFilter(this);
    resetInactivityTimer();
}

VideoPlayWindow::~VideoPlayWindow() {
#ifdef CAR_DESK_USE_T507_SDK
    releaseSdkPlayer();
#endif

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
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        centralWidget->setAttribute(Qt::WA_TranslucentBackground, true);
        centralWidget->setAutoFillBackground(false);
        centralWidget->setStyleSheet("background: transparent;");
    }
#endif
    setCentralWidget(centralWidget);
    centralWidget->setAttribute(Qt::WA_AcceptTouchEvents);
    centralWidget->installEventFilter(this);
    
    // 视频显示区域（全屏填充）
    m_videoWidget->setParent(centralWidget);
    m_videoWidget->setGeometry(0, 0, 1280, 720);
    m_videoWidget->setStyleSheet(m_useSdkPlayer ? "background: transparent;" : "background: black;");
    m_videoWidget->setAttribute(Qt::WA_AcceptTouchEvents);
    m_videoWidget->installEventFilter(this);
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        m_videoWidget->hide();
    }
#endif
    
    // ===== 顶部栏：绝对定位 =====
    m_topBar = new QWidget(centralWidget);
    m_topBar->setGeometry(0, 0, 1280, 72);
    m_topBar->setStyleSheet("background: rgba(0, 0, 0, 0.5);");
    m_topBar->raise();  // 确保在视频上层

    // 返回按钮（HTML: .video_play_top span）
    m_backButton->setParent(m_topBar);
    m_backButton->setFixedSize(48, 48);
    m_backButton->move(12, 12);
    m_backButton->setFocusPolicy(Qt::NoFocus);
    m_backButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background-color: transparent; "
        "  background-image: url(:/images/butt_video_back_up.png); "
        "  background-repeat: no-repeat; "
        "  background-position: center; "
        "  outline: none; "
        "} "
        "QPushButton:hover { "
        "  background-image: url(:/images/butt_video_back_down.png); "
        "} "
        "QPushButton:focus { "
        "  outline: none; "
        "  border: none; "
        "}"
    );

    // 标题（HTML: .video_play_top h1）
    m_titleLabel->setParent(m_topBar);
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
    m_bottomBar = new QWidget(centralWidget);
    m_bottomBar->setGeometry(0, 720 - 132, 1280, 132);
    m_bottomBar->setStyleSheet("background: rgba(0, 0, 0, 0.5);");
    m_bottomBar->raise();  // 确保在视频上层

    // HTML: padding 24px 48px, display:flex
    QHBoxLayout *bottomLayout = new QHBoxLayout(m_bottomBar);
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
    m_prevButton->setFocusPolicy(Qt::NoFocus);
    m_prevButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "  outline: none; "
        "} "
        "QPushButton:focus { "
        "  outline: none; "
        "  border: none; "
        "}"
    );

    // 播放/暂停
    m_playButton->setFixedSize(84, 84);
    m_playButton->setIcon(QIcon(":/images/butt_music_play_up.png"));
    m_playButton->setIconSize(QSize(84, 84));
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "  outline: none; "
        "} "
        "QPushButton:focus { "
        "  outline: none; "
        "  border: none; "
        "}"
    );

    // 下一首
    m_nextButton->setFixedSize(60, 60);
    m_nextButton->setIcon(QIcon(":/images/butt_music_next_up.png"));
    m_nextButton->setIconSize(QSize(60, 60));
    m_nextButton->setFocusPolicy(Qt::NoFocus);
    m_nextButton->setStyleSheet(
        "QPushButton { "
        "  border: none; "
        "  background: transparent; "
        "  outline: none; "
        "} "
        "QPushButton:focus { "
        "  outline: none; "
        "  border: none; "
        "}"
    );

    buttonLayout->addWidget(m_prevButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_playButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_nextButton);

    // 右侧控制区域（HTML: .video_play_control）
    QWidget *controlWidget = new QWidget();
    controlWidget->setStyleSheet("background: transparent;");
    QHBoxLayout *controlLayout = new QHBoxLayout(controlWidget);
    controlLayout->setContentsMargins(48, 0, 0, 0);
    controlLayout->setSpacing(0);

    m_timeLabel->setStyleSheet("color: #fff; font-size: 24px; background: transparent;");
    m_timeLabel->setFixedWidth(100);
    m_timeLabel->setAlignment(Qt::AlignCenter);

    QWidget *progressContainer = new QWidget();
    progressContainer->setStyleSheet("background: transparent;");
    progressContainer->installEventFilter(this);
    m_progressContainer = progressContainer;
    QHBoxLayout *progressLayout = new QHBoxLayout(progressContainer);
    progressLayout->setContentsMargins(24, 14, 24, 14);
    progressLayout->setSpacing(0);

    // 使用真正的滑块作为进度条
    m_progressSlider->setFixedHeight(8);
    m_progressSlider->setMinimum(0);
    m_progressSlider->setMaximum(1000);
    m_progressSlider->setValue(0);
    m_progressSlider->setStyleSheet(
        "QSlider { "
        "  background: transparent; "
        "  margin: 0; "
        "} "
        "QSlider::groove:horizontal { "
        "  background: transparent; "
        "  height: 8px; "
        "  border-radius: 4px; "
        "  border: none; "
        "} "
        "QSlider::handle:horizontal { "
        "  background: #fff; "
        "  width: 16px; "
        "  height: 16px; "
        "  border-radius: 8px; "
        "  margin: -4px 0; "
        "  border: none; "
        "} "
        "QSlider::sub-page:horizontal { "
        "  background: #00a0e9; "
        "  border-radius: 4px; "
        "  border: none; "
        "} "
        "QSlider::add-page:horizontal { "
        "  background: transparent; "
        "  border: none; "
        "}"
    );

    progressLayout->addWidget(m_progressSlider);

    m_durationLabel->setStyleSheet("color: #fff; font-size: 24px; background: transparent;");
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

#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (!initSdkPlayer(videoPath)) {
            setPlayButtonState(false);
            qWarning() << "Failed to init XPlayer backend:" << videoPath;
            return;
        }

        m_sdkPlaying = true;
        setPlayButtonState(true);
        if (m_sdkTimer && !m_sdkTimer->isActive()) {
            m_sdkTimer->start();
        }
        return;
    }
#endif

    if (!m_mediaPlayer) {
        return;
    }
    m_mediaPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(videoPath)));
    m_mediaPlayer->play();
}

void VideoPlayWindow::onNextVideo() {
    if (m_videoFiles.isEmpty()) {
        return;
    }
    m_currentIndex = (m_currentIndex + 1) % m_videoFiles.count();
    onPlayVideo();
}

void VideoPlayWindow::onPreviousVideo() {
    if (m_videoFiles.isEmpty()) {
        return;
    }
    m_currentIndex = (m_currentIndex - 1 + m_videoFiles.count()) % m_videoFiles.count();
    onPlayVideo();
}

void VideoPlayWindow::updateTitle() {
    if (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count()) {
        QFileInfo fileInfo(m_videoFiles[m_currentIndex]);
        m_titleLabel->setText(fileInfo.baseName());
    }
}

void VideoPlayWindow::setVideoFiles(const QStringList &files, int currentIndex) {
    m_pausedForHome = false;  // 选择了新视频，放弃原暂停状态
    m_resumePath.clear();
    m_resumePositionMs = 0;
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
        if (m_videoFiles.isEmpty()) {
            return;
        }
        // 播放完成，自动播放列表中的下一首（最后一首时循环到第一首）
        onNextVideo();
    }
}

void VideoPlayWindow::onPlaybackStateChanged(QMediaPlayer::State state) {
    qDebug() << "Playback state changed:" << state;
    setPlayButtonState(state == QMediaPlayer::PlayingState);
}

void VideoPlayWindow::onPositionChanged(qint64 position) {
    if (!m_mediaPlayer) {
        return;
    }
    updateTimeAndSlider(position, m_mediaPlayer->duration());
}

void VideoPlayWindow::onDurationChanged(qint64 duration) {
    if (!m_mediaPlayer) {
        return;
    }
    updateTimeAndSlider(m_mediaPlayer->position(), duration);
}

void VideoPlayWindow::setPlayButtonState(bool playing)
{
    m_playButton->setIcon(QIcon());
    m_playButton->repaint();
    if (playing) {
        m_playButton->setIcon(QIcon(":/images/butt_music_stop_up.png"));
    } else {
        m_playButton->setIcon(QIcon(":/images/butt_music_play_up.png"));
    }
}

void VideoPlayWindow::updateTimeAndSlider(qint64 positionMs, qint64 durationMs)
{
    if (positionMs < 0) {
        positionMs = 0;
    }
    if (durationMs < 0) {
        durationMs = 0;
    }

    int seconds = static_cast<int>(positionMs / 1000);
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    m_timeLabel->setText(QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));

    int totalSeconds = static_cast<int>(durationMs / 1000);
    int totalHours = totalSeconds / 3600;
    int totalMinutes = (totalSeconds % 3600) / 60;
    int totalSecs = totalSeconds % 60;
    m_durationLabel->setText(QString("%1:%2:%3")
        .arg(totalHours, 2, 10, QChar('0'))
        .arg(totalMinutes, 2, 10, QChar('0'))
        .arg(totalSecs, 2, 10, QChar('0')));

    if (!m_progressSlider->isSliderDown() && durationMs > 0) {
        int sliderValue = static_cast<int>((positionMs * 1000) / durationMs);
        if (sliderValue < 0) sliderValue = 0;
        if (sliderValue > 1000) sliderValue = 1000;
        m_progressSlider->setValue(sliderValue);
    }
}

void VideoPlayWindow::resetInactivityTimer()
{
    if (!m_hideTimer) {
        return;
    }

    if (m_controlsHidden) {
        return;
    }

    m_hideTimer->stop();
    m_hideTimer->start();
}

void VideoPlayWindow::hideControls()
{
    if (m_controlsHidden) {
        return;
    }
    m_controlsHidden = true;
    if (m_topBar) {
        m_topBar->hide();
    }
    if (m_bottomBar) {
        m_bottomBar->hide();
    }
}

void VideoPlayWindow::showControls()
{
    if (!m_controlsHidden) {
        return;
    }
    m_controlsHidden = false;
    if (m_topBar) {
        m_topBar->show();
    }
    if (m_bottomBar) {
        m_bottomBar->show();
    }
    resetInactivityTimer();
}

void VideoPlayWindow::handleUserActivity()
{
    if (m_controlsHidden) {
        showControls();
        return;
    }
    resetInactivityTimer();
}

bool VideoPlayWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchEnd:
    case QEvent::MouseMove:
    case QEvent::KeyPress:
    case QEvent::Wheel:
        handleUserActivity();
        break;
    default:
        break;
    }
    return QMainWindow::event(event);
}

void VideoPlayWindow::mousePressEvent(QMouseEvent *event)
{
    handleUserActivity();
    QMainWindow::mousePressEvent(event);
}

void VideoPlayWindow::mouseReleaseEvent(QMouseEvent *event)
{
    handleUserActivity();
    QMainWindow::mouseReleaseEvent(event);
}

void VideoPlayWindow::mouseMoveEvent(QMouseEvent *event)
{
    handleUserActivity();
    QMainWindow::mouseMoveEvent(event);
}

void VideoPlayWindow::wheelEvent(QWheelEvent *event)
{
    handleUserActivity();
    QMainWindow::wheelEvent(event);
}

bool VideoPlayWindow::eventFilter(QObject *obj, QEvent *event)
{
    QWidget *widget = qobject_cast<QWidget*>(obj);
    if (!widget || widget->window() != this) {
        return QMainWindow::eventFilter(obj, event);
    }

    if (widget == m_progressContainer) {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() == Qt::LeftButton) {
                int value = sliderValueFromContainerPos(mouseEvent->pos());
                beginSliderSeek(value);
                return true;
            }
            if (event->type() == QEvent::MouseMove && m_sliderDragging) {
                int value = sliderValueFromContainerPos(mouseEvent->pos());
                previewSliderSeek(value);
                return true;
            }
            if (event->type() == QEvent::MouseButtonRelease && mouseEvent->button() == Qt::LeftButton && m_sliderDragging) {
                int value = sliderValueFromContainerPos(mouseEvent->pos());
                finalizeSliderSeek(value);
                return true;
            }
        }
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchEnd:
    case QEvent::MouseMove:
    case QEvent::KeyPress:
    case QEvent::Wheel:
        handleUserActivity();
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(obj, event);
}

int VideoPlayWindow::sliderValueFromContainerPos(const QPoint &pos) const
{
    if (!m_progressSlider) {
        return 0;
    }

    QPoint sliderPos = m_progressSlider->mapFromParent(pos);
    int x = sliderPos.x();
    int min = m_progressSlider->minimum();
    int max = m_progressSlider->maximum();
    int width = m_progressSlider->width();
    if (width <= 0) {
        return min;
    }
    int value = QStyle::sliderValueFromPosition(min, max, x, width, false);
    return qBound(min, value, max);
}

void VideoPlayWindow::beginSliderSeek(int value)
{
    if (m_sliderDragging) {
        return;
    }
    m_sliderDragging = true;
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        m_wasPlayingBeforeSeek = m_sdkPlaying;
        if (m_sdkPlaying && m_sdkPlayer) {
            XPlayerPause(m_sdkPlayer);
            m_sdkPlaying = false;
            setPlayButtonState(false);
        }
    }
#endif
    if (!m_mediaPlayer) {
        return;
    }
    m_wasPlayingBeforeSeek = (m_mediaPlayer->state() == QMediaPlayer::PlayingState);
    if (m_wasPlayingBeforeSeek) {
        m_mediaPlayer->pause();
    }
    previewSliderSeek(value);
}

void VideoPlayWindow::previewSliderSeek(int value)
{
    if (!m_progressSlider) {
        return;
    }
    m_progressSlider->setValue(value);
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkDurationMs > 0) {
        qint64 positionMs = (static_cast<qint64>(value) * m_sdkDurationMs) / 1000;
        updateTimeAndSlider(positionMs, m_sdkDurationMs);
        return;
    }
#endif
    if (!m_mediaPlayer) {
        return;
    }
    qint64 durationMs = m_mediaPlayer->duration();
    if (durationMs > 0) {
        qint64 positionMs = (static_cast<qint64>(value) * durationMs) / 1000;
        updateTimeAndSlider(positionMs, durationMs);
    }
}

void VideoPlayWindow::finalizeSliderSeek(int value)
{
    if (!m_sliderDragging) {
        return;
    }
    m_sliderDragging = false;
    previewSliderSeek(value);

#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkPlayer && m_sdkDurationMs > 0) {
        const qint64 positionMs = (static_cast<qint64>(value) * m_sdkDurationMs) / 1000;
        XPlayerSeekTo(m_sdkPlayer, static_cast<int>(positionMs), AW_SEEK_CLOSEST_SYNC);
        if (m_wasPlayingBeforeSeek) {
            XPlayerStart(m_sdkPlayer);
            m_sdkPlaying = true;
            setPlayButtonState(true);
            if (m_sdkTimer && !m_sdkTimer->isActive()) {
                m_sdkTimer->start();
            }
        }
        return;
    }
#endif
    if (!m_mediaPlayer) {
        return;
    }
    qint64 durationMs = m_mediaPlayer->duration();
    if (durationMs > 0) {
        qint64 position = (static_cast<qint64>(value) * durationMs) / 1000;
        m_mediaPlayer->setPosition(position);
    }
    if (m_wasPlayingBeforeSeek && !m_mediaPlayer->media().isNull()) {
        m_mediaPlayer->play();
    }
}

void VideoPlayWindow::onSdkTick()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_useSdkPlayer || !m_sdkPlayer) {
        return;
    }

    int posMs = 0;
    if (XPlayerGetCurrentPosition(m_sdkPlayer, &posMs) == 0) {
        updateTimeAndSlider(posMs, m_sdkDurationMs);
    }
#endif
}

void VideoPlayWindow::onSdkPlaybackComplete()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_useSdkPlayer) {
        return;
    }
    if (m_sdkSwitching) {
        return;
    }

    m_sdkPlaying = false;
    setPlayButtonState(false);
    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }

    m_sdkSwitching = true;
    QTimer::singleShot(300, this, [this]() {
        m_sdkSwitching = false;   // 先清标志，再执行切换，避免 initSdkPlayer 守卫误拦截
        onNextVideo();
    });
#endif
}

void VideoPlayWindow::onSdkSeekComplete()
{
    // 由 sdkPlayerNotify 经 QueuedConnection 在 Qt 主线程调用。
    // seek 已完成，清除标志；若此时 player 已被 releaseSdkPlayer 释放则为无效操作。
#ifdef CAR_DESK_USE_T507_SDK
    m_sdkSeeking = false;
    if (m_pendingRelease) {
        // releaseSdkPlayer 在 seek 期间被推迟了。
        // seek 已安全结束，现在执行真正的 Pause + Reset。
        releaseSdkPlayer();
    }
#endif
}

#ifdef CAR_DESK_USE_T507_SDK
bool VideoPlayWindow::initSdkPlayer(const QString &videoPath)
{
    // 确保全局 SDK 资源已创建（进程生命周期内只创建一次）
    if (!ensureSdkResourcesCreated()) {
        qWarning() << "Failed to create global SDK resources";
        return false;
    }

    // 若上一次 seek 仍在进行（pendingRelease），禁止重新初始化，
    // 避免在 seek 活跃期间操作 XPlayer 导致状态错乱。
    if (m_pendingRelease) {
        qWarning() << "initSdkPlayer: deferred release pending, cannot init now";
        return false;
    }

    // 停止当前播放、解绑旧回调（releaseSdkPlayer 内调 XPlayerReset）
    releaseSdkPlayer();

    // 复用全局播放器
    m_sdkPlayer    = g_sdkPlayer;
    m_sdkLayerCtrl = g_sdkLayerCtrl;
    m_sdkSoundCtrl = g_sdkSoundCtrl;

    // 绑定当前窗口回调
    XPlayerSetNotifyCallback(m_sdkPlayer, sdkPlayerNotify, this);

    const QByteArray pathBytes = videoPath.toLocal8Bit();
    if (XPlayerSetDataSourceUrl(m_sdkPlayer, pathBytes.constData(), nullptr, nullptr) != 0) {
        qWarning() << "XPlayerSetDataSourceUrl failed:" << videoPath;
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        return false;
    }

    if (XPlayerPrepare(m_sdkPlayer) != 0) {
        qWarning() << "XPlayerPrepare failed:" << videoPath;
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerReset(m_sdkPlayer);
        return false;
    }

    int durationMs = 0;
    if (XPlayerGetDuration(m_sdkPlayer, &durationMs) == 0) {
        m_sdkDurationMs = durationMs;
    } else {
        m_sdkDurationMs = 0;
    }
    updateTimeAndSlider(0, m_sdkDurationMs);

    if (XPlayerStart(m_sdkPlayer) != 0) {
        qWarning() << "XPlayerStart failed:" << videoPath;
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerReset(m_sdkPlayer);
        return false;
    }

    return true;
}

void VideoPlayWindow::releaseSdkPlayer()
{
    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }
    m_sdkPlaying = false;
    m_sdkDurationMs = 0;

    if (m_sdkPlayer) {
        if (m_sdkSeeking) {
            // XPlayerReset（及 XPlayerPause）在 seek 进行中调用会导致 SDK 内部崩溃。
            // 保留回调和 m_sdkPlayer 存活，待 onSdkSeekComplete 确认 seek
            // 安全结束后再执行真正的 Pause + Reset。
            m_pendingRelease = true;
            m_sdkSoundCtrl = nullptr;
            m_sdkLayerCtrl = nullptr;
            m_sdkSubCtrl = nullptr;
            m_sdkDi = nullptr;
            return;  // 提前返回，不 Reset，不 null m_sdkPlayer
        }
        // 先解绑回调，防止停止过程中产生悬空回调。
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerPause(m_sdkPlayer);
        XPlayerReset(m_sdkPlayer);
        m_sdkPlayer = nullptr;
    }
    m_sdkSeeking = false;
    m_pendingRelease = false;
    m_sdkSoundCtrl = nullptr;
    m_sdkLayerCtrl = nullptr;
    m_sdkSubCtrl = nullptr;
    m_sdkDi = nullptr;
}
#endif

void VideoPlayWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, this);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, this);
        break;
    case Qt::Key_HomePage:
        // 保存当前位置 + 完整释放 SDK（XPlayerReset 清除硬件叠加层）
        // m_sdkSeeking 标志保护 releaseSdkPlayer：seek 进行中时跳过 XPlayerPause 直接 Reset
        m_pausedForHome = false;
        m_resumePath.clear();
        m_resumePositionMs = 0;
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer && m_sdkPlayer) {
            int curPos = 0;
            if (XPlayerGetCurrentPosition(m_sdkPlayer, &curPos) == 0) {
                m_resumePositionMs = curPos;
            }
            m_resumePath = m_videoFiles.value(m_currentIndex);
            m_pausedForHome = !m_resumePath.isEmpty();
            releaseSdkPlayer();  // XPlayerReset 清除叠加层；hideEvent 中 m_sdkPlayer==nullptr 为空操作
        }
#endif
        if (!m_useSdkPlayer && m_mediaPlayer) {
            if (m_mediaPlayer->state() == QMediaPlayer::PlayingState
                    || m_mediaPlayer->state() == QMediaPlayer::PausedState) {
                m_resumePath = m_videoFiles.value(m_currentIndex);
                m_resumePositionMs = static_cast<int>(m_mediaPlayer->position());
                m_pausedForHome = !m_resumePath.isEmpty();
                m_mediaPlayer->pause();
            }
        }
        emit requestReturnToMain();
        hide();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        emit requestReturnToList();
        hide();  // hideEvent 负责 releaseSdkPlayer()
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void VideoPlayWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        releaseSdkPlayer();  // HOME 路径已由 keyPressEvent 释放，m_sdkPlayer==nullptr 此处为空操作
    }
#endif
}

void VideoPlayWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    showControls();
    resetInactivityTimer();
    if (!m_pausedForHome || m_resumePath.isEmpty()) {
        return;
    }
    const QString path = m_resumePath;
    const int posMs = m_resumePositionMs;
    m_pausedForHome = false;
    m_resumePath.clear();
    m_resumePositionMs = 0;
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (initSdkPlayer(path)) {
            m_sdkPlaying = true;
            setPlayButtonState(true);
            if (posMs > 0) {
                // 给解码器 400ms 启动时间后 Seek 到 HOME 前的位置
                // m_sdkSeeking 标志由此进入，SEEK_COMPLETE 后由 onSdkSeekComplete 清除
                QTimer::singleShot(400, this, [this, posMs]() {
                    if (m_sdkPlayer && m_sdkPlaying) {
                        m_sdkSeeking = true;
                        XPlayerSeekTo(m_sdkPlayer, posMs, AW_SEEK_CLOSEST_SYNC);
                    }
                });
            }
            if (m_sdkTimer && !m_sdkTimer->isActive()) {
                m_sdkTimer->start();
            }
        }
        return;
    }
#endif
    if (!m_useSdkPlayer && m_mediaPlayer) {
        m_mediaPlayer->play();  // PC 路径：媒体仍在暂停状态，直接恢复
    }
}
