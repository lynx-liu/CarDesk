#include "videoplaywindow.h"
#include "ahdrecordstore.h"
#include "bluetoothmanager.h"
#include "devicedetect.h"
#include "t507sdkbridge.h"
#include "xplayercedarx.h"
#include "appsignals.h"
#include "touchclicksound.h"

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
    , m_timeLabel(new QLabel("01:24:20", this))
    , m_durationLabel(new QLabel("01:44:48", this))
    , m_prevButton(new QPushButton(this))
    , m_playButton(new QPushButton(this))
    , m_nextButton(new QPushButton(this))
    , m_backButton(new QPushButton(this))
    , m_progressSlider(new QSlider(Qt::Horizontal, this))
    , m_progressContainer(nullptr)
    , m_topBar(nullptr)
    , m_bottomBar(nullptr)
    , m_speedWarningLabel(nullptr)
    , m_currentIndex(-1)
    , m_playerProcess(nullptr)
    , m_hideTimer(nullptr)
    , m_mediaPlayer(nullptr)
    , m_videoWidget(nullptr)
    , m_useSdkPlayer(false)
    , m_controlsHidden(false)
    , m_sliderDragging(false)
    , m_bluetoothManager(nullptr)
    , m_wasPlayingBeforeSeek(false)
    , m_pausedForHome(false)
    , m_pausedForOcclusion(false)
    , m_pausedForInterruption(false)
    , m_speedHighLocked(false)
    , m_resumePositionMs(0)
    , m_resumeInterruptPositionMs(0)
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
    , m_switchPending(false)
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
    
    connect(AppSignals::instance(), &AppSignals::vehicleSpeedChanged,
            this, [this](float speedKmh) {
        if (!m_speedWarningLabel) return;
        if (speedKmh >= 10.0f) {
            if (!m_speedHighLocked) {
                m_speedHighLocked = true;
                m_playButton->setEnabled(false);
                m_prevButton->setEnabled(false);
                m_nextButton->setEnabled(false);
                if (m_progressSlider) {
                    m_progressSlider->setEnabled(false);
                }
                m_speedWarningLabel->show();
                m_speedWarningLabel->raise();
                pauseIfPlaying();
            }
        } else {
            if (m_speedHighLocked) {
                m_speedHighLocked = false;
                m_playButton->setEnabled(true);
                m_prevButton->setEnabled(true);
                m_nextButton->setEnabled(true);
                if (m_progressSlider) {
                    m_progressSlider->setEnabled(true);
                }
            }
            m_speedWarningLabel->hide();
            // 不自动恢复播放
        }
    });

    connect(AppSignals::instance(), &AppSignals::recordFilesChanged, this, [this]() {
        if (!isDrivingRecordPlayback() || !isVisible()) {
            return;
        }
        refreshRecordPlaylistIfNeeded();
    });

    setupUI();
    loadVideoFiles();
    
    connect(m_backButton, &QPushButton::clicked, this, [this]() {
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            forceReleaseSdkPlayer();
        }
#endif
        emit requestReturnToList();
        hide();
    });
    connect(m_playButton, &QPushButton::clicked, this, [this]() {
        if (m_speedHighLocked) return;  // 车速过高，禁止手动播放
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
                if (TouchClickSound::isBusy()) {
                    QTimer::singleShot(150, this, [this]() {
                        if (!m_speedHighLocked) {
                            m_playButton->click();
                        }
                    });
                    return;
                }
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

void VideoPlayWindow::setBluetoothManager(BluetoothManager *manager) {
    m_bluetoothManager = manager;
}

bool VideoPlayWindow::hasPendingResumeFor(VideoPlaybackOrigin origin) const
{
    return hasPendingResume() && m_playbackOrigin == origin;
}

VideoPlayWindow::~VideoPlayWindow() {
#ifdef CAR_DESK_USE_T507_SDK
    forceReleaseSdkPlayer();
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

    // ===== 车速过高提示遮罩（默认隐藏，创建于所有控件之后确保 z-order 最高）=====
    m_speedWarningLabel = new QLabel(QStringLiteral("车速过高，无法播放视频"), centralWidget);
    m_speedWarningLabel->setGeometry(0, 0, 1280, 720);
    m_speedWarningLabel->setAlignment(Qt::AlignCenter);
    m_speedWarningLabel->setStyleSheet(
        "QLabel{"
        "  background:rgba(0,0,0,0.72);"
        "  color:#fff;"
        "  font-size:48px;"
        "  font-weight:bold;"
        "}"
    );
    m_speedWarningLabel->hide();
    m_speedWarningLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
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
    if (m_speedHighLocked) {
        return;
    }
    if (TouchClickSound::isBusy()) {
        QTimer::singleShot(150, this, [this]() {
            if (!TouchClickSound::isBusy()) {
                onPlayVideo();
            }
        });
        return;
    }
    if (isDrivingRecordPlayback() && !ensureCurrentFilePlayable()) {
        qWarning() << "No playable record file in playlist";
        return;
    }
    if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        m_currentIndex = 0;
    }
    
    if (m_currentIndex >= m_videoFiles.count()) {
        return;
    }
    
    const QString videoPath = m_videoFiles[m_currentIndex];
    qDebug() << "Playing video:" << videoPath;
    if (m_bluetoothManager) {
        m_bluetoothManager->stopMusic();
    }
    T507SdkBridge::setAudioSource(false);
    updateTitle();

#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (m_pausedForInterruption) {
            if (restoreSdkPlaybackAfterInterruption()) {
                m_sdkPlaying = true;
                setPlayButtonState(true);
                if (m_sdkTimer && !m_sdkTimer->isActive()) {
                    m_sdkTimer->start();
                }
                return;
            }
        }
        if (m_sdkSwitching) {
            m_switchPending = true;
            return;
        }
        if (m_sdkPlayer && !m_sdkPlaying) {
            if (XPlayerStart(m_sdkPlayer) == 0) {
                m_sdkPlaying = true;
                setPlayButtonState(true);
                if (m_sdkTimer && !m_sdkTimer->isActive()) {
                    m_sdkTimer->start();
                }
                return;
            }
        }

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

    if (m_pausedForInterruption
            && !m_mediaPlayer->media().isNull()
            && m_mediaPlayer->media().canonicalUrl() == QUrl::fromLocalFile(videoPath)) {
        if (m_resumeInterruptPositionMs > 0) {
            m_mediaPlayer->setPosition(m_resumeInterruptPositionMs);
        }
        m_mediaPlayer->play();
        setPlayButtonState(true);
        m_pausedForInterruption = false;
        m_resumeInterruptPositionMs = 0;
        return;
    }

    m_mediaPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(videoPath)));
    m_mediaPlayer->play();
}

void VideoPlayWindow::onNextVideo() {
    if (m_speedHighLocked || m_videoFiles.isEmpty()) {
        return;
    }
    m_switchDirection = 1;
    if (isDrivingRecordPlayback()) {
        if (!selectAdjacentRecordFile(1)) {
            return;
        }
    } else {
        m_currentIndex = (m_currentIndex + 1) % m_videoFiles.count();
    }
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        updateTitle();
        requestSdkVideoSwitch();
        return;
    }
#endif
    onPlayVideo();
}

void VideoPlayWindow::onPreviousVideo() {
    if (m_speedHighLocked || m_videoFiles.isEmpty()) {
        return;
    }
    m_switchDirection = -1;
    if (isDrivingRecordPlayback()) {
        if (!selectAdjacentRecordFile(-1)) {
            return;
        }
    } else {
        m_currentIndex = (m_currentIndex - 1 + m_videoFiles.count()) % m_videoFiles.count();
    }
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        updateTitle();
        requestSdkVideoSwitch();
        return;
    }
#endif
    onPlayVideo();
}

void VideoPlayWindow::updateTitle() {
    if (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count()) {
        QFileInfo fileInfo(m_videoFiles[m_currentIndex]);
        m_titleLabel->setText(fileInfo.baseName());
    }
}

bool VideoPlayWindow::isDrivingRecordPlayback() const
{
    return m_playbackOrigin == VideoPlaybackOrigin::DrivingRecord && !m_recordDateKey.isEmpty();
}

void VideoPlayWindow::reloadDrivingRecordPlaylist()
{
    if (m_recordDateKey.isEmpty()) {
        return;
    }
    m_videoFiles = AhdRecordStore::filterExistingFiles(
        AhdRecordStore::listVideoFilesForDate(m_recordDateKey));
}

bool VideoPlayWindow::selectAdjacentRecordFile(int direction)
{
    if (!isDrivingRecordPlayback()) {
        return false;
    }

    const QString playingPath = (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count())
        ? m_videoFiles.at(m_currentIndex)
        : QString();
    reloadDrivingRecordPlaylist();
    if (m_videoFiles.isEmpty()) {
        m_currentIndex = -1;
        return false;
    }

    int idx = playingPath.isEmpty() ? m_currentIndex : m_videoFiles.indexOf(playingPath);
    if (idx < 0) {
        idx = qBound(0, m_currentIndex, m_videoFiles.count() - 1);
    }

    const int count = m_videoFiles.count();
    for (int step = 1; step <= count; ++step) {
        const int tryIdx = direction >= 0
            ? (idx + step) % count
            : (idx - step + count) % count;
        if (QFileInfo::exists(m_videoFiles.at(tryIdx))) {
            m_currentIndex = tryIdx;
            return true;
        }
    }

    m_currentIndex = -1;
    return false;
}

void VideoPlayWindow::refreshRecordPlaylistIfNeeded()
{
    if (!isDrivingRecordPlayback()) {
        return;
    }

    const QString anchorPath = (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count())
        ? m_videoFiles.at(m_currentIndex)
        : QString();
    reloadDrivingRecordPlaylist();
    if (m_videoFiles.isEmpty()) {
        m_currentIndex = -1;
        return;
    }

    if (!anchorPath.isEmpty()) {
        const int idx = m_videoFiles.indexOf(anchorPath);
        if (idx >= 0) {
            m_currentIndex = idx;
            return;
        }
    }
    m_currentIndex = qBound(0, m_currentIndex, m_videoFiles.count() - 1);
}

bool VideoPlayWindow::ensureCurrentFilePlayable()
{
    if (!isDrivingRecordPlayback()) {
        return m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count();
    }

    refreshRecordPlaylistIfNeeded();
    if (m_videoFiles.isEmpty() || m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        return false;
    }
    if (QFileInfo::exists(m_videoFiles.at(m_currentIndex))) {
        return true;
    }

    return selectAdjacentRecordFile(m_switchDirection >= 0 ? 1 : -1);
}

void VideoPlayWindow::setVideoFiles(const QStringList &files, int currentIndex,
                                    VideoPlaybackOrigin origin) {
    m_pausedForHome = false;
    m_pausedForInterruption = false;
    m_resumeInterruptPositionMs = 0;
    m_resumePath.clear();
    m_resumePositionMs = 0;
    m_playbackOrigin = origin;
    m_recordDateKey.clear();
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        forceReleaseSdkPlayer();
    }
#endif
    m_videoFiles = files;
    m_currentIndex = currentIndex;
    if (!files.isEmpty()) {
        const int anchorIdx = qBound(0, currentIndex, files.count() - 1);
        if (origin == VideoPlaybackOrigin::DrivingRecord
            || cedarxIsAhdRecordVideoPath(files.at(anchorIdx))) {
            m_recordDateKey = AhdRecordStore::dateKeyForFile(files.at(anchorIdx));
            refreshRecordPlaylistIfNeeded();
            if (m_videoFiles.isEmpty()) {
                m_currentIndex = -1;
            } else if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
                m_currentIndex = 0;
            }
        }
    }
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
        m_topBar->lower();
    }
    if (m_bottomBar) {
        m_bottomBar->hide();
        m_bottomBar->lower();
    }

    if (QWidget *cw = centralWidget()) {
        cw->update();
        cw->repaint();
    }
    update();
}

void VideoPlayWindow::showControls()
{
    if (!m_controlsHidden) {
        return;
    }
    m_controlsHidden = false;
    if (m_topBar) {
        m_topBar->show();
        m_topBar->raise();
    }
    if (m_bottomBar) {
        m_bottomBar->show();
        m_bottomBar->raise();
    }
    resetInactivityTimer();
    refreshControlsProgressUi();
}

void VideoPlayWindow::refreshControlsProgressUi()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_useSdkPlayer || !m_sdkPlayer || m_controlsHidden) {
        return;
    }
    int posMs = 0;
    if (XPlayerGetCurrentPosition(m_sdkPlayer, &posMs) == 0) {
        updateTimeAndSlider(posMs, m_sdkDurationMs);
    }
#endif
}

void VideoPlayWindow::handleUserActivity()
{
    if (m_controlsHidden) {
        showControls();
        return;
    }
    resetInactivityTimer();
    refreshControlsProgressUi();
}

bool VideoPlayWindow::event(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow()) {
            if (isVisible()) {
#ifdef CAR_DESK_USE_T507_SDK
                if ((m_useSdkPlayer && m_sdkPlayer && m_sdkPlaying) ||
                    (!m_useSdkPlayer && m_mediaPlayer && m_mediaPlayer->state() == QMediaPlayer::PlayingState)) {
                    m_pausedForOcclusion = true;
                    pauseIfPlaying();
                }
#else
                if (m_mediaPlayer && m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
                    m_pausedForOcclusion = true;
                    pauseIfPlaying();
                }
#endif
            }
        } else if (m_pausedForOcclusion) {
            m_pausedForOcclusion = false;
#ifdef CAR_DESK_USE_T507_SDK
            if (m_useSdkPlayer) {
                if (m_sdkPlayer) {
                    XPlayerStart(m_sdkPlayer);
                    m_sdkPlaying = true;
                    setPlayButtonState(true);
                    if (m_sdkTimer && !m_sdkTimer->isActive()) {
                        m_sdkTimer->start();
                    }
                } else {
                    onPlayVideo();
                }
            } else
#endif
            {
                if (m_mediaPlayer) {
                    T507SdkBridge::setAudioSource(false);
                    m_mediaPlayer->play();
                }
            }
        }
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::TouchBegin:
    case QEvent::TouchEnd:
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
        if (!m_controlsHidden) {
            if (m_topBar) {
                m_topBar->raise();
            }
            if (m_bottomBar) {
                m_bottomBar->raise();
            }
        }
    }
#endif
}

void VideoPlayWindow::onSdkPlaybackComplete()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_useSdkPlayer || m_videoFiles.isEmpty()) {
        return;
    }

    m_sdkPlaying = false;
    setPlayButtonState(false);
    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }

    m_switchDirection = 1;
    if (isDrivingRecordPlayback()) {
        if (!selectAdjacentRecordFile(1)) {
            return;
        }
    } else {
        m_currentIndex = (m_currentIndex + 1) % m_videoFiles.count();
    }
    updateTitle();
    requestSdkVideoSwitch();
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
    if (m_sdkSwitching && isVisible()) {
        QMetaObject::invokeMethod(this, "continueSdkVideoSwitch", Qt::QueuedConnection);
    }
#endif
}

#ifdef CAR_DESK_USE_T507_SDK
void VideoPlayWindow::requestSdkVideoSwitch()
{
    if (m_speedHighLocked || !m_useSdkPlayer || m_videoFiles.isEmpty()) {
        return;
    }
    if (isDrivingRecordPlayback() && !ensureCurrentFilePlayable()) {
        return;
    }
    if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        return;
    }

    if (m_sdkSwitching) {
        m_switchPending = true;
        return;
    }

    beginSdkVideoSwitch();
}

void VideoPlayWindow::beginSdkVideoSwitch()
{
    m_sdkSwitching = true;
    m_switchPending = false;

    if (!m_speedHighLocked) {
        m_prevButton->setEnabled(false);
        m_nextButton->setEnabled(false);
    }
    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }
    m_sdkPlaying = false;
    setPlayButtonState(false);

    releaseSdkPlayer();
    if (m_pendingRelease) {
        return;
    }

    if (!isVisible()) {
        m_sdkSwitching = false;
        if (!m_speedHighLocked) {
            m_prevButton->setEnabled(true);
            m_nextButton->setEnabled(true);
        }
        return;
    }

    QMetaObject::invokeMethod(this, "continueSdkVideoSwitch", Qt::QueuedConnection);
}

bool VideoPlayWindow::initSdkPlayer(const QString &videoPath)
{
    if (!ensureSdkResourcesCreated()) {
        qWarning() << "Failed to create global SDK resources";
        return false;
    }

    releaseSdkPlayer();
    if (m_pendingRelease) {
        forceReleaseSdkPlayer();
    }

    return startSdkPlayer(videoPath);
}

bool VideoPlayWindow::startSdkPlayer(const QString &videoPath)
{
    if (!g_sdkPlayer) {
        return false;
    }

    m_sdkPlayer    = g_sdkPlayer;
    m_sdkLayerCtrl = g_sdkLayerCtrl;
    m_sdkSoundCtrl = g_sdkSoundCtrl;

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
        m_sdkPlayer = nullptr;
        return false;
    }

    int durationMs = 0;
    if (XPlayerGetDuration(m_sdkPlayer, &durationMs) == 0) {
        m_sdkDurationMs = durationMs;
    } else {
        m_sdkDurationMs = 0;
    }
    updateTimeAndSlider(0, m_sdkDurationMs);

    if (cedarxIsAhdRecordVideoPath(videoPath)) {
        cedarxXPlayerSetDiscardAudio(m_sdkPlayer, 1);
        qDebug() << "[Video] AHD record playback: discard audio (skip 4s AV first-sync wait)";
    } else {
        cedarxXPlayerSetDiscardAudio(m_sdkPlayer, 0);
    }

    if (XPlayerStart(m_sdkPlayer) != 0) {
        qWarning() << "XPlayerStart failed:" << videoPath;
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerReset(m_sdkPlayer);
        m_sdkPlayer = nullptr;
        return false;
    }

    qDebug() << "VideoPlayWindow::startSdkPlayer:" << videoPath << "durationMs=" << m_sdkDurationMs;
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

void VideoPlayWindow::forceReleaseSdkPlayer()
{
    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }
    m_sdkPlaying = false;
    m_sdkDurationMs = 0;
    m_sdkSeeking = false;
    m_pendingRelease = false;
    m_sdkSwitching = false;
    m_switchPending = false;

    if (m_sdkPlayer) {
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerPause(m_sdkPlayer);
        XPlayerReset(m_sdkPlayer);
        m_sdkPlayer = nullptr;
    }
    m_sdkSoundCtrl = nullptr;
    m_sdkLayerCtrl = nullptr;
    m_sdkSubCtrl = nullptr;
    m_sdkDi = nullptr;
}

void VideoPlayWindow::resetSdkPlayerForCall()
{
    if (!m_useSdkPlayer || !m_sdkPlayer) {
        return;
    }

    if (m_sdkTimer) {
        m_sdkTimer->stop();
    }
    m_sdkPlaying = false;
    m_pausedForInterruption = true;
    if (m_sdkPlayer) {
        XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
        XPlayerReset(m_sdkPlayer);
        m_sdkPlayer = nullptr;
    }
}

bool VideoPlayWindow::restoreSdkPlaybackAfterInterruption()
{
    qDebug() << "VideoPlayWindow::restoreSdkPlaybackAfterInterruption: useSdk=" << m_useSdkPlayer
             << " sdkPlayer=" << static_cast<void*>(m_sdkPlayer)
             << " pausedForInterruption=" << m_pausedForInterruption
             << " currentIndex=" << m_currentIndex
             << " resumeInterruptPositionMs=" << m_resumeInterruptPositionMs;
    if (!m_useSdkPlayer || m_sdkPlayer || !m_pausedForInterruption || m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        qWarning() << "VideoPlayWindow::restoreSdkPlaybackAfterInterruption: invalid state, cannot restore";
        return false;
    }

    const QString videoPath = m_videoFiles[m_currentIndex];
    if (!initSdkPlayer(videoPath)) {
        qWarning() << "VideoPlayWindow::restoreSdkPlaybackAfterInterruption: initSdkPlayer failed for" << videoPath;
        return false;
    }

    if (m_resumeInterruptPositionMs > 0) {
        qDebug() << "VideoPlayWindow::restoreSdkPlaybackAfterInterruption: seeking to" << m_resumeInterruptPositionMs;
        m_sdkSeeking = true;
        XPlayerSeekTo(m_sdkPlayer, m_resumeInterruptPositionMs, AW_SEEK_CLOSEST_SYNC);
    }

    m_pausedForInterruption = false;
    m_resumeInterruptPositionMs = 0;
    return true;
}
#endif

void VideoPlayWindow::continueSdkVideoSwitch()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (!m_sdkSwitching) {
        return;
    }
    if (!isVisible()) {
        m_sdkSwitching = false;
        m_switchPending = false;
        if (!m_speedHighLocked) {
            m_prevButton->setEnabled(true);
            m_nextButton->setEnabled(true);
        }
        return;
    }
    if (m_pendingRelease) {
        return;
    }
    if (isDrivingRecordPlayback() && !ensureCurrentFilePlayable()) {
        m_sdkSwitching = false;
        if (!m_speedHighLocked) {
            m_prevButton->setEnabled(true);
            m_nextButton->setEnabled(true);
        }
        return;
    }
    if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        m_sdkSwitching = false;
        if (!m_speedHighLocked) {
            m_prevButton->setEnabled(true);
            m_nextButton->setEnabled(true);
        }
        return;
    }

    const QString videoPath = m_videoFiles.at(m_currentIndex);
    bool ok = startSdkPlayer(videoPath);

    if (!ok && isDrivingRecordPlayback() && selectAdjacentRecordFile(m_switchDirection)) {
        updateTitle();
        ok = startSdkPlayer(m_videoFiles.at(m_currentIndex));
    }

    m_sdkSwitching = false;
    if (!m_speedHighLocked) {
        m_prevButton->setEnabled(true);
        m_nextButton->setEnabled(true);
    }

    if (ok) {
        m_sdkPlaying = true;
        setPlayButtonState(true);
        showControls();
        resetInactivityTimer();
        refreshControlsProgressUi();
        if (m_sdkTimer && !m_sdkTimer->isActive()) {
            m_sdkTimer->start();
        }
    } else {
        setPlayButtonState(false);
    }

    if (m_switchPending) {
        m_switchPending = false;
        beginSdkVideoSwitch();
    }
#endif
}

void VideoPlayWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::changeVolume(+1);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::changeVolume(-1);
        break;
    case Qt::Key_MediaPrevious:
        onPreviousVideo();
        break;
    case Qt::Key_MediaNext:
        onNextVideo();
        break;
    case Qt::Key_MediaPlay:
    case Qt::Key_MediaPause:
    case Qt::Key_MediaTogglePlayPause:
        onPlayVideo();
        break;
    case Qt::Key_HomePage:
        // HOME 路径：直接释放 SDK 播放器（归还 PCM 设备），记录进度，
        // 回来时通过 resumeAfterInterruption 路径从断点恢复。
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            if (m_sdkPlayer && (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count())) {
                int curPos = 0;
                XPlayerGetCurrentPosition(m_sdkPlayer, &curPos);
                m_resumeInterruptPositionMs = curPos;
                m_pausedForInterruption = true;
            } else {
                m_pausedForInterruption = false;
                m_resumeInterruptPositionMs = 0;
            }
            m_pausedForHome = false;
            m_pausedForOcclusion = false;
            forceReleaseSdkPlayer();
        } else
#endif
        {
            // 非 SDK（PC 预览）保持原有暂停行为
            m_pausedForHome = (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count());
            if (m_pausedForHome) {
                m_pausedForInterruption = false;
                pauseIfPlaying();
            }
        }
        emit requestReturnToMain();
        hide();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            forceReleaseSdkPlayer();
        }
#endif
        emit requestReturnToList();
        hide();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

bool VideoPlayWindow::isPlaying() const
{
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) return m_sdkPlaying;
#endif
    return m_mediaPlayer && m_mediaPlayer->state() == QMediaPlayer::PlayingState;
}

QString VideoPlayWindow::currentVideoPath() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_videoFiles.count()) {
        return m_videoFiles.at(m_currentIndex);
    }
    return QString();
}

void VideoPlayWindow::pauseIfPlaying()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkPlayer && m_sdkPlaying) {
        int curPos = 0;
        if (XPlayerGetCurrentPosition(m_sdkPlayer, &curPos) == 0) {
            m_resumeInterruptPositionMs = curPos;
        }
        m_pausedForOcclusion = true;
        XPlayerPause(m_sdkPlayer);
        m_sdkPlaying = false;
        setPlayButtonState(false);
        return;
    }
#endif
    if (m_mediaPlayer && m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
        m_resumeInterruptPositionMs = static_cast<int>(m_mediaPlayer->position());
        m_pausedForOcclusion = true;
        m_mediaPlayer->pause();
        setPlayButtonState(false);
    }
}

void VideoPlayWindow::pauseForInterruption()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkPlayer) {
        int curPos = 0;
        if (XPlayerGetCurrentPosition(m_sdkPlayer, &curPos) == 0) {
            m_resumeInterruptPositionMs = curPos;
        }
        m_pausedForInterruption = true;
        resetSdkPlayerForCall();
        setPlayButtonState(false);
        return;
    }
#endif
    if (m_mediaPlayer &&
        (m_mediaPlayer->state() == QMediaPlayer::PlayingState ||
         m_mediaPlayer->state() == QMediaPlayer::PausedState)) {
        m_resumeInterruptPositionMs = static_cast<int>(m_mediaPlayer->position());
        m_pausedForInterruption = true;
        if (m_mediaPlayer->state() == QMediaPlayer::PlayingState) {
            m_mediaPlayer->pause();
        }
        setPlayButtonState(false);
    }
}

void VideoPlayWindow::resumeAfterInterruption()
{
    qDebug() << "VideoPlayWindow::resumeAfterInterruption: pausedForInterruption=" << m_pausedForInterruption
             << " pausedForOcclusion=" << m_pausedForOcclusion
             << " currentIndex=" << m_currentIndex
             << " resumeInterruptPositionMs=" << m_resumeInterruptPositionMs;
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        T507SdkBridge::setAudioSource(false);
        if (m_pausedForInterruption) {
            if (restoreSdkPlaybackAfterInterruption()) {
                m_sdkPlaying = true;
                setPlayButtonState(true);
                if (m_sdkTimer && !m_sdkTimer->isActive()) {
                    m_sdkTimer->start();
                }
                m_pausedForOcclusion = false;
                m_resumeInterruptPositionMs = 0;
                return;
            }
            qWarning() << "VideoPlayWindow::resumeAfterInterruption: restoreSdkPlaybackAfterInterruption failed";
        }
        if (m_sdkPlayer && !m_sdkPlaying) {
            if (XPlayerStart(m_sdkPlayer) == 0) {
                m_sdkPlaying = true;
                setPlayButtonState(true);
                if (m_sdkTimer && !m_sdkTimer->isActive()) {
                    m_sdkTimer->start();
                }
            }
        }
    } else
#endif
    {
        if (m_mediaPlayer) {
            T507SdkBridge::setAudioSource(false);
            m_mediaPlayer->play();
            setPlayButtonState(true);
        }
    }
    m_pausedForOcclusion = false;
    m_resumeInterruptPositionMs = 0;
}

void VideoPlayWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && !m_pausedForOcclusion && !m_pausedForHome && !m_pausedForInterruption) {
        forceReleaseSdkPlayer();
    }
#endif
}

void VideoPlayWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    showControls();
    resetInactivityTimer();
    if (m_pausedForHome) {
        m_pausedForHome = false;
        // HOME 恢复交给后续 pausedForOcclusion/pausedForInterruption 分支统一处理。
    }

    if (m_pausedForOcclusion || m_pausedForInterruption) {
        if (m_pausedForInterruption) {
            resumeAfterInterruption();
            return;
        }

#ifdef CAR_DESK_USE_T507_SDK
        if (m_useSdkPlayer) {
            T507SdkBridge::setAudioSource(false);
            if (m_sdkPlayer && !m_sdkPlaying) {
                if (XPlayerStart(m_sdkPlayer) == 0) {
                    m_sdkPlaying = true;
                    setPlayButtonState(true);
                    if (m_sdkTimer && !m_sdkTimer->isActive()) {
                        m_sdkTimer->start();
                    }
                }
            } else if (!m_sdkPlayer) {
                onPlayVideo();
            }
        } else
#endif
        {
            if (m_mediaPlayer && m_mediaPlayer->state() != QMediaPlayer::PlayingState) {
                T507SdkBridge::setAudioSource(false);
                m_mediaPlayer->play();
                setPlayButtonState(true);
            }
        }
        m_pausedForOcclusion = false;
        return;
    }

    if (m_currentIndex < 0 || m_currentIndex >= m_videoFiles.count()) {
        return;
    }

    const QString currentVideo = m_videoFiles.value(m_currentIndex);
    if (currentVideo.isEmpty()) {
        return;
    }

#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (m_sdkPlayer) {
            XPlayerStart(m_sdkPlayer);
            m_sdkPlaying = true;
            setPlayButtonState(true);
            if (m_sdkTimer && !m_sdkTimer->isActive()) {
                m_sdkTimer->start();
            }
            return;
        }
        onPlayVideo();
        return;
    }
#endif

    if (!m_mediaPlayer) {
        return;
    }

    if (!m_mediaPlayer->media().isNull() && m_mediaPlayer->state() == QMediaPlayer::PausedState) {
        T507SdkBridge::setAudioSource(false);
        m_mediaPlayer->play();
        return;
    }

    if (m_mediaPlayer->state() != QMediaPlayer::PlayingState) {
        onPlayVideo();
    }
}
