#include "musicplayerwindow.h"
#include "devicedetect.h"
#include "topbarwidget.h"
#include "appsignals.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QProcess>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QSettings>
#include <QScreen>
#include <QApplication>
#include <QMouseEvent>
#include <QStyle>
#include <QTextCodec>
#include <QScroller>
#include <QTime>

// ══════════════════════════════════════════════════════════════════════════════
// T507 SDK 全局音频资源（进程内单例，只创建一次）
// ══════════════════════════════════════════════════════════════════════════════
#ifdef CAR_DESK_USE_T507_SDK

static XPlayer   *g_sdkMusicPlayer    = nullptr;
static SoundCtrl *g_sdkMusicSoundCtrl = nullptr;

static int sdkMusicNotify(void *pUser, int msg, int /*ext1*/, void * /*para*/)
{
    MusicPlayerWindow *w = static_cast<MusicPlayerWindow *>(pUser);
    if (!w) return 0;
    switch (msg) {
    case AWPLAYER_MEDIA_PLAYBACK_COMPLETE:
        QMetaObject::invokeMethod(w, "onSdkPlaybackComplete", Qt::QueuedConnection);
        break;
    case AWPLAYER_MEDIA_ERROR:
        qWarning() << "MusicSDK: playback error";
        break;
    default:
        break;
    }
    return 0;
}

static bool ensureSdkMusicResourcesCreated()
{
    if (g_sdkMusicPlayer) return true;

    g_sdkMusicSoundCtrl = SoundDeviceCreate();
    if (!g_sdkMusicSoundCtrl) {
        qWarning() << "MusicSDK: SoundDeviceCreate failed";
        return false;
    }
    g_sdkMusicPlayer = XPlayerCreate();
    if (!g_sdkMusicPlayer) {
        qWarning() << "MusicSDK: XPlayerCreate failed";
        return false;
    }
    if (XPlayerInitCheck(g_sdkMusicPlayer) != 0) {
        qWarning() << "MusicSDK: XPlayerInitCheck failed";
        return false;
    }
    XPlayerSetAudioSink(g_sdkMusicPlayer, g_sdkMusicSoundCtrl);
    qDebug() << "MusicSDK: global audio resources created";
    return true;
}

#endif // CAR_DESK_USE_T507_SDK

class MusicPlayerWindow;

class MusicProgressSlider : public QSlider {
public:
    MusicProgressSlider(MusicPlayerWindow *owner, Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
        , m_owner(owner) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (orientation() == Qt::Horizontal && event->button() == Qt::LeftButton) {
            int value = QStyle::sliderValueFromPosition(minimum(), maximum(), event->pos().x(), width(), false);
            setValue(value);
        }
        QSlider::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QSlider::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && m_owner) {
            m_owner->processSliderRelease(value());
        }
    }

private:
    MusicPlayerWindow *m_owner;
};

// ══════════════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ══════════════════════════════════════════════════════════════════════════════

MusicPlayerWindow::MusicPlayerWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentBrowsePath("/mnt")
    , m_mediaPlayer(new QMediaPlayer(this))
{
    setWindowTitle("音乐播放");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else {
        if (QApplication::primaryScreen())
            move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

#ifdef CAR_DESK_USE_T507_SDK
    m_useSdkPlayer = (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT);
    m_sdkTimer = new QTimer(this);
    m_sdkTimer->setInterval(500);
    connect(m_sdkTimer, &QTimer::timeout, this, &MusicPlayerWindow::onSdkTick);
#endif

    setupUI();

    // ── QMediaPlayer 信号（PC 端）──
    connect(m_mediaPlayer, &QMediaPlayer::positionChanged,
            this, &MusicPlayerWindow::onMediaPositionChanged);
    connect(m_mediaPlayer, &QMediaPlayer::durationChanged,
            this, &MusicPlayerWindow::onMediaDurationChanged);
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged,
            this, &MusicPlayerWindow::onMediaStatusChanged);
    connect(m_mediaPlayer, &QMediaPlayer::stateChanged,
            this, &MusicPlayerWindow::onMediaStateChanged);

    // 初始扫描文件
    scanFlatPlaylist();
    refreshPlaylistWidget();
    loadFavoriteSongs();
    updateCollectButtonState();

    qDebug() << "MusicPlayerWindow created, found" << m_musicFiles.count() << "audio files";
}

MusicPlayerWindow::~MusicPlayerWindow()
{
    releaseAudioPlayer();
}

void MusicPlayerWindow::closeEvent(QCloseEvent *event)
{
    releaseAudioPlayer();
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

// ══════════════════════════════════════════════════════════════════════════════
// setupUI — 创建两页 QStackedWidget
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::setupUI()
{
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setGeometry(0, 0, 1280, 720);
    setCentralWidget(m_stackedWidget);

    QWidget *playerPage = new QWidget();
    playerPage->setFixedSize(1280, 720);
    setupPlayerPage(playerPage);
    m_stackedWidget->addWidget(playerPage);   // index 0

    QWidget *listPage = new QWidget();
    listPage->setFixedSize(1280, 720);
    setupListPage(listPage);
    m_stackedWidget->addWidget(listPage);     // index 1

    m_stackedWidget->setCurrentIndex(kPagePlayer);
}

// ══════════════════════════════════════════════════════════════════════════════
// setupPlayerPage — 匹配 music_usb_play.html（绝对坐标）
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::setupPlayerPage(QWidget *page)
{
    page->setStyleSheet("background-image: url(:/images/inside_background.png);");

    // ── 顶部栏 (0,0,1280,82 topbar.png) ──────────────────────────────────
    QWidget *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat; background-size: cover;");

    // HOME 按钮 (12,12,48,48)
    m_homeButton = new QPushButton(topBar);
    m_homeButton->setGeometry(12, 12, 48, 48);
    m_homeButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_home_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/pict_home_down.png); }");
    m_homeButton->setCursor(Qt::PointingHandCursor);

    // 标题（居中，y:10, 字号:36）
    m_titleLabel = new QLabel("音频播放", topBar);
    m_titleLabel->setGeometry(0, 10, 1280, 54);
    m_titleLabel->setStyleSheet("color: #fff; font-size: 36px; font-weight: 700; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // 状态图标（使用 TopBarRightWidget 自动同步音量和时钟）
    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    // ── Tab 标签栏（y:100，USB:480,100,160×66 | BT:640,100,160×66）─────
    m_usbTab = new QPushButton("USB音乐", page);
    m_usbTab->setGeometry(480, 100, 160, 66);
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }");
    m_usbTab->setCursor(Qt::PointingHandCursor);

    m_btTab = new QPushButton("蓝牙音乐", page);
    m_btTab->setGeometry(640, 100, 160, 66);
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }");
    m_btTab->setCursor(Qt::PointingHandCursor);

    // ── 专辑封面（120,182,210,210）────────────────────────────────────────
    QLabel *albumImg = new QLabel(page);
    albumImg->setGeometry(120, 182, 210, 210);
    albumImg->setPixmap(QPixmap(":/images/music_show.png").scaled(210, 210, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // ── 歌曲名（x:410, y:183, 字号:48, 行高:72）─────────────────────────
    m_nowPlayingLabel = new QLabel("未播放", page);
    m_nowPlayingLabel->setGeometry(410, 183, 650, 72);
    m_nowPlayingLabel->setStyleSheet("color: #fff; font-size: 48px; background: transparent;");
    m_nowPlayingLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // ── 歌手（图标 410,283,40×40；文字 466,279,字号:32）─────────────────
    QLabel *singerIcon = new QLabel(page);
    singerIcon->setGeometry(410, 283, 40, 40);
    singerIcon->setPixmap(QPixmap(":/images/pict_music_singer_icon.png").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    singerIcon->setStyleSheet("background: transparent;");

    QLabel *singerLbl = new QLabel("--", page);
    singerLbl->setGeometry(466, 279, 600, 48);
    singerLbl->setStyleSheet("color: #fff; font-size: 32px; background: transparent;");
    singerLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // ── 专辑（图标 410,347,40×40；文字 466,343,字号:32）─────────────────
    QLabel *albumIcon = new QLabel(page);
    albumIcon->setGeometry(410, 347, 40, 40);
    albumIcon->setPixmap(QPixmap(":/images/pict_music_album_icon.png").scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    albumIcon->setStyleSheet("background: transparent;");

    QLabel *albumLbl = new QLabel("--", page);
    albumLbl->setGeometry(466, 343, 600, 48);
    albumLbl->setStyleSheet("color: #fff; font-size: 32px; background: transparent;");
    albumLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // ── 功能按钮（收藏 1100,190,60×60；扫描 1100,324,60×60）─────────────
    QPushButton *collectBtn = new QPushButton(page);
    collectBtn->setGeometry(1100, 190, 60, 60);
    collectBtn->setCheckable(true);
    collectBtn->setAutoDefault(false);
    collectBtn->setDefault(false);
    collectBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_collection_up.png); background-repeat: no-repeat; }"
        "QPushButton:pressed { background-image: url(:/images/butt_music_collection_down.png); }"
        "QPushButton:checked { background-image: url(:/images/butt_music_collection_down.png); }"
        "QPushButton:checked:pressed { background-image: url(:/images/butt_music_collection_down.png); }");
    collectBtn->setCursor(Qt::PointingHandCursor);

    QPushButton *scanBtn = new QPushButton(page);
    scanBtn->setGeometry(1100, 324, 60, 60);
    scanBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_scan_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_scan_down.png); }");
    scanBtn->setCursor(Qt::PointingHandCursor);
    m_collectButton = collectBtn;
    connect(collectBtn, &QPushButton::clicked, this, &MusicPlayerWindow::onToggleCollect);
    connect(scanBtn, &QPushButton::clicked, this, &MusicPlayerWindow::onRescan);

    // ── 进度区域 ─────────────────────────────────────────────────────────
    // 播放时间（x:120, y:408, 字号:24, 行高:36）
    m_posLabel = new QLabel("00:00", page);
    m_posLabel->setGeometry(120, 408, 80, 36);
    m_posLabel->setStyleSheet("color: #fff; font-size: 24px; background: transparent;");
    m_posLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // 总长时间（x:1100, y:408）
    m_durLabel = new QLabel("00:00", page);
    m_durLabel->setGeometry(1100, 408, 80, 36);
    m_durLabel->setStyleSheet("color: #fff; font-size: 24px; background: transparent;");
    m_durLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // 进度滑块（204,422,872×8 → slider 竖向居中；widget 更高但视觉轨道保持 8px）
    m_progressSlider = new MusicProgressSlider(this, Qt::Horizontal, page);
    m_progressSlider->setGeometry(204, 412, 872, 26);
    m_progressSlider->setMinimum(0);
    m_progressSlider->setMaximum(1000);
    m_progressSlider->setValue(0);
    m_progressSlider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  border: none; height: 8px; margin: 9px 0;"
        "  background: url(:/images/prog_music_back.png) no-repeat left center; }"
        "QSlider::sub-page:horizontal {"
        "  height: 8px; margin: 9px 0;"
        "  background: url(:/images/prog_music_fore.png) no-repeat left center; }"
        "QSlider::add-page:horizontal {"
        "  height: 8px; margin: 9px 0; background: transparent; }"
        "QSlider::handle:horizontal {"
        "  background: #ffffff; border: none;"
        "  width: 14px; height: 14px; border-radius: 7px; margin: -3px 0; }");
    connect(m_progressSlider, &QSlider::sliderPressed, this, [this]() {
        if (!m_sliderDragging)
            beginSliderSeek(m_progressSlider->value());
    });
    connect(m_progressSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_sliderDragging)
            previewSliderSeek(value);
    });
    connect(m_progressSlider, &QSlider::sliderReleased, this, [this]() {
        if (m_sliderDragging)
            finalizeSliderSeek(m_progressSlider->value());
    });

    // ── 控制按钮（精确坐标）──────────────────────────────────────────────
    // 列表 238,466,60×60
    m_listButton = new QPushButton(page);
    m_listButton->setGeometry(238, 466, 60, 60);
    m_listButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_list_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_list_down.png); }");
    m_listButton->setCursor(Qt::PointingHandCursor);

    // 上一首 418,466,60×60
    m_prevButton = new QPushButton(page);
    m_prevButton->setGeometry(418, 466, 60, 60);
    m_prevButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_prev_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_prev_down.png); }");
    m_prevButton->setCursor(Qt::PointingHandCursor);

    // 播放/暂停 598,454,84×84
    m_playButton = new QPushButton(page);
    m_playButton->setGeometry(598, 454, 84, 84);
    m_playButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_play_up.png); background-repeat: no-repeat; }"
        "QPushButton:pressed { background-image: url(:/images/butt_music_play_down.png); }");
    m_playButton->setCursor(Qt::PointingHandCursor);

    // 下一首 802,466,60×60
    m_nextButton = new QPushButton(page);
    m_nextButton->setGeometry(802, 466, 60, 60);
    m_nextButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_next_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_next_down.png); }");
    m_nextButton->setCursor(Qt::PointingHandCursor);

    // 循环/随机 982,466,60×60
    m_loopButton = new QPushButton(page);
    m_loopButton->setGeometry(982, 466, 60, 60);
    m_loopButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_circle_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_circle_down.png); }");
    m_loopButton->setCursor(Qt::PointingHandCursor);

    // ── 底部水平播放列表（120,562,1040×120）─────────────────────────────
    m_playlistWidget = new QListWidget(page);
    m_playlistWidget->setGeometry(120, 562, 1040, 120);
    m_playlistWidget->setFlow(QListView::LeftToRight);
    m_playlistWidget->setWrapping(false);
    m_playlistWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlistWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlistWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_playlistWidget->setGridSize(QSize(144, 120));
    m_playlistWidget->setMovement(QListView::Static);
    m_playlistWidget->setResizeMode(QListView::Fixed);
    m_playlistWidget->setUniformItemSizes(true);
    m_playlistWidget->setStyleSheet("QListWidget { background: transparent; border: none; outline: none; }");
    m_playlistWidget->setItemDelegate(new MusicPlaylistDelegate(m_playlistWidget));
    QScroller::grabGesture(m_playlistWidget->viewport(), QScroller::LeftMouseButtonGesture);

    // ── 按钮信号 ─────────────────────────────────────────────────────────
    connect(m_homeButton,     &QPushButton::clicked, this, [this]() {
        releaseAudioPlayer();
        emit requestReturnToMain();
        hide();
    });
    connect(m_usbTab,         &QPushButton::clicked, this, &MusicPlayerWindow::onUsbTabClicked);
    connect(m_btTab,          &QPushButton::clicked, this, &MusicPlayerWindow::onBtTabClicked);
    connect(m_listButton,     &QPushButton::clicked, this, &MusicPlayerWindow::onOpenListPage);
    connect(m_prevButton,     &QPushButton::clicked, this, &MusicPlayerWindow::onPreviousMusic);
    connect(m_playButton,     &QPushButton::clicked, this, &MusicPlayerWindow::onPlayPause);
    connect(m_nextButton,     &QPushButton::clicked, this, &MusicPlayerWindow::onNextMusic);
    connect(m_playlistWidget, &QListWidget::itemClicked,
            this, &MusicPlayerWindow::onPlaylistItemClicked);

    connect(m_loopButton, &QPushButton::clicked, this, [this]() {
        static bool isRandom = false;
        isRandom = !isRandom;
        m_loopButton->setStyleSheet(isRandom
            ? "QPushButton { border: none; background-image: url(:/images/butt_music_random_up.png); background-repeat: no-repeat; }"
              "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_random_down.png); }"
            : "QPushButton { border: none; background-image: url(:/images/butt_music_circle_up.png); background-repeat: no-repeat; }"
              "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_music_circle_down.png); }");
    });

    Q_UNUSED(m_progressSlider);
}

// ══════════════════════════════════════════════════════════════════════════════
// setupListPage — 匹配 music_usb_play_list.html（绝对坐标）
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::setupListPage(QWidget *page)
{
    page->setStyleSheet("background-image: url(:/images/inside_background.png);");

    // ── 顶部栏（与播放页完全相同）────────────────────────────────────────
    QWidget *topBar = new QWidget(page);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat; background-size: cover;");

    // HOME 按钮（标题栏内 x:12, y:17）
    auto *listTopHomeBtn = new QPushButton(topBar);
    listTopHomeBtn->setGeometry(12, 17, 48, 48);
    listTopHomeBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_home_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/pict_home_down.png); }");
    listTopHomeBtn->setCursor(Qt::PointingHandCursor);
    connect(listTopHomeBtn, &QPushButton::clicked, this, [this]() {
        releaseAudioPlayer();
        emit requestReturnToMain();
        hide();
    });

    // 标题
    QLabel *titleLbl = new QLabel("音频播放", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color: #fff; font-size: 36px; font-weight: 700; background: transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    listTopHomeBtn->raise();

    // 状态图标（TopBarRightWidget 自动同步音量和时钟）
    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

    // ── 返回按钮（x:60, y:103, 60×60）───────────────────────────────────
    m_backFromListButton = new QPushButton(page);
    m_backFromListButton->setGeometry(60, 103, 60, 60);
    m_backFromListButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_back_up.png); background-repeat: no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image: url(:/images/butt_back_down.png); }");
    m_backFromListButton->setCursor(Qt::PointingHandCursor);


    // ── Tab 标签栏（我的收藏 480,100,160×66 | 歌曲列表 640,100,160×66）──
    m_listFavTab = new QPushButton("我的收藏", page);
    m_listFavTab->setGeometry(480, 100, 160, 66);
    m_listFavTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }");
    m_listFavTab->setCursor(Qt::PointingHandCursor);

    m_listSongsTab = new QPushButton("歌曲列表", page);
    m_listSongsTab->setGeometry(640, 100, 160, 66);
    m_listSongsTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }");
    m_listSongsTab->setCursor(Qt::PointingHandCursor);

    // ── 音乐网格列表（x:168, y:190, 944×356，与视频列表相同布局）─────────
    m_musicListWidget = new QListWidget(page);
    m_musicListWidget->setGeometry(168, 190, 944, 356);
    m_musicListWidget->setViewMode(QListView::IconMode);
    m_musicListWidget->setMovement(QListView::Static);
    m_musicListWidget->setResizeMode(QListView::Fixed);
    m_musicListWidget->setSpacing(0);
    m_musicListWidget->setGridSize(QSize(188, 178));
    m_musicListWidget->setUniformItemSizes(true);
    m_musicListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_musicListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_musicListWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_musicListWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { border: none; }");
    m_musicListWidget->setItemDelegate(new MusicListItemDelegate(m_musicListWidget));
    QScroller::grabGesture(m_musicListWidget->viewport(), QScroller::LeftMouseButtonGesture);

    // ── 路径标签（x:168, y:590, 944×50）─────────────────────────────────
    // 样式：rgba(255,255,255,.1) 背景 + 1px #0068FF border + border-radius:5px
    m_listPathLabel = new QLabel("/mnt", page);
    m_listPathLabel->setGeometry(168, 590, 944, 50);
    m_listPathLabel->setStyleSheet(
        "QLabel { background: rgba(255,255,255,0.1); border: 1px solid #0068FF; border-radius: 5px;"
        "  color: #fff; font-size: 24px; padding-left: 24px; }");
    m_listPathLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // ── 按钮信号 ─────────────────────────────────────────────────────────
    connect(m_backFromListButton, &QPushButton::clicked, this, &MusicPlayerWindow::onBackFromListPage);
    connect(m_listSongsTab, &QPushButton::clicked, this, &MusicPlayerWindow::onListSongsTabClicked);
    connect(m_listFavTab,   &QPushButton::clicked, this, &MusicPlayerWindow::onListFavTabClicked);
    connect(m_musicListWidget, &QListWidget::itemClicked,
            this, &MusicPlayerWindow::onMusicListItemClicked);
}

// ══════════════════════════════════════════════════════════════════════════════
// 目录加载（列表页）
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::loadDirectory(const QString &path)
{
    m_currentBrowsePath = path;
    m_musicListWidget->clear();

    QDir dir(path);
    if (!dir.exists()) return;

    dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    dir.setSorting(QDir::DirsFirst | QDir::Name);

    // 更新路径标签（USB > subdir 格式）
    QString displayPath = path;
    if (displayPath.startsWith("/mnt"))
        displayPath = "USB" + displayPath.mid(4).replace('/', " > ");
    m_listPathLabel->setText(displayPath.isEmpty() ? "USB" : displayPath);

    for (const QFileInfo &fi : dir.entryInfoList()) {
        bool isDir = fi.isDir();
        bool isAudio = !isDir && m_audioExtensions.contains(fi.suffix().toLower());
        if (!isDir && !isAudio) continue;

        QListWidgetItem *item = new QListWidgetItem(fi.fileName(), m_musicListWidget);
        item->setData(Qt::UserRole,     fi.absoluteFilePath());
        item->setData(Qt::UserRole + 1, isDir);
        m_musicListWidget->addItem(item);
    }

    qDebug() << "MusicList: loaded" << m_musicListWidget->count() << "items from" << path;
}

void MusicPlayerWindow::loadFavoriteSongs()
{
    QSettings settings;
    m_favoriteFiles = settings.value("music/favorites").toStringList();
}

void MusicPlayerWindow::saveFavoriteSongs()
{
    QSettings settings;
    settings.setValue("music/favorites", m_favoriteFiles);
}

void MusicPlayerWindow::updateCollectButtonState()
{
    if (!m_collectButton) return;
    bool favorited = false;
    if (m_currentIndex >= 0 && m_currentIndex < m_musicFiles.count())
        favorited = m_favoriteFiles.contains(m_musicFiles[m_currentIndex]);

    m_collectButton->setChecked(favorited);
    m_collectButton->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/butt_music_collection_up.png); background-repeat: no-repeat; }"
        "QPushButton:pressed { background-image: url(:/images/butt_music_collection_down.png); }"
        "QPushButton:checked { background-image: url(:/images/butt_music_collection_down.png); }"
        "QPushButton:checked:pressed { background-image: url(:/images/butt_music_collection_down.png); }");
}

void MusicPlayerWindow::refreshFavoriteList()
{
    if (!m_musicListWidget) return;
    m_musicListWidget->clear();
    for (const QString &filePath : qAsConst(m_favoriteFiles)) {
        QFileInfo fi(filePath);
        QListWidgetItem *item = new QListWidgetItem(fi.fileName(), m_musicListWidget);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        item->setData(Qt::UserRole + 1, false);
        m_musicListWidget->addItem(item);
    }
    m_listPathLabel->setText("我的收藏");
}

void MusicPlayerWindow::onToggleCollect()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_musicFiles.count()) return;
    const QString path = m_musicFiles[m_currentIndex];
    if (m_favoriteFiles.contains(path))
        m_favoriteFiles.removeAll(path);
    else
        m_favoriteFiles << path;
    m_favoriteFiles.removeDuplicates();
    saveFavoriteSongs();
    updateCollectButtonState();
    if (m_listFavMode)
        refreshFavoriteList();
}

// ══════════════════════════════════════════════════════════════════════════════
// 递归扫描平铺播放列表（播放页用）
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::scanFlatPlaylist()
{
    m_musicFiles.clear();

    QStringList scanRoots;
    scanRoots << "/mnt";
#ifndef CAR_DESK_DEVICE_CARUNIT
    scanRoots << QDir::homePath() + "/Music"
              << QDir::homePath() + "/music"
              << QDir::homePath() + "/Downloads";
#endif

    for (const QString &root : scanRoots) {
        if (!QDir(root).exists()) continue;
        QList<QString> stack;
        stack << root;
        while (!stack.isEmpty()) {
            QDir d(stack.takeFirst());
            d.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            for (const QFileInfo &fi : d.entryInfoList()) {
                if (fi.isDir())
                    stack << fi.absoluteFilePath();
                else if (m_audioExtensions.contains(fi.suffix().toLower()))
                    m_musicFiles << fi.absoluteFilePath();
            }
        }
    }
    qDebug() << "MusicPlayer: scanned" << m_musicFiles.count() << "audio files";
}

void MusicPlayerWindow::refreshPlaylistWidget()
{
    if (!m_playlistWidget) return;
    m_playlistWidget->clear();
    for (const QString &f : m_musicFiles) {
        QFileInfo fi(f);
        QListWidgetItem *item = new QListWidgetItem(fi.baseName(), m_playlistWidget);
        m_playlistWidget->addItem(item);
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_playlistWidget->count()) {
        m_playlistWidget->setCurrentRow(m_currentIndex);
        m_playlistWidget->scrollToItem(m_playlistWidget->currentItem(), QAbstractItemView::PositionAtCenter);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// 播放控制
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::playMusic(int index)
{
    if (index < 0 || index >= m_musicFiles.count()) return;
    m_currentIndex = index;

    releaseAudioPlayer();
    updateNowPlaying();
    if (m_playlistWidget)
        m_playlistWidget->setCurrentRow(m_currentIndex);
    updateCollectButtonState();

    const QString musicPath = m_musicFiles[m_currentIndex];
    qDebug() << "MusicPlayer: playing" << musicPath;

#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (!ensureSdkMusicResourcesCreated()) return;

        m_sdkPlayer    = g_sdkMusicPlayer;
        m_sdkSoundCtrl = g_sdkMusicSoundCtrl;
        XPlayerSetNotifyCallback(m_sdkPlayer, sdkMusicNotify, this);

        const QByteArray pb = musicPath.toUtf8();
        if (XPlayerSetDataSourceUrl(m_sdkPlayer, pb.constData(), nullptr, nullptr) != 0) {
            qWarning() << "MusicSDK: SetDataSourceUrl failed"; return;
        }
        if (XPlayerPrepare(m_sdkPlayer) != 0) {
            qWarning() << "MusicSDK: Prepare failed";
            XPlayerReset(m_sdkPlayer); return;
        }
        int durMs = 0;
        if (XPlayerGetDuration(m_sdkPlayer, &durMs) == 0) m_sdkDurationMs = durMs;
        else m_sdkDurationMs = 0;
        updateProgressBar(0, m_sdkDurationMs);

        if (XPlayerStart(m_sdkPlayer) != 0) {
            qWarning() << "MusicSDK: Start failed";
            XPlayerReset(m_sdkPlayer); return;
        }
        m_sdkPlaying = true;
        setPlayButtonState(true);
        if (m_sdkTimer && !m_sdkTimer->isActive()) m_sdkTimer->start();
        return;
    }
#endif

    m_mediaPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(musicPath)));
    m_mediaPlayer->play();
}

void MusicPlayerWindow::releaseAudioPlayer()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (m_sdkTimer) m_sdkTimer->stop();
        m_sdkPlaying = false;
        m_sdkDurationMs = 0;
        if (m_sdkPlayer) {
            XPlayerSetNotifyCallback(m_sdkPlayer, nullptr, nullptr);
            XPlayerReset(m_sdkPlayer);  // 永不调 XPlayerDestroy
            m_sdkPlayer = nullptr;
            m_sdkSoundCtrl = nullptr;
        }
        setPlayButtonState(false);
        updateProgressBar(0, 0);
        return;
    }
#endif
    if (m_mediaPlayer) m_mediaPlayer->stop();
    updateProgressBar(0, 0);
}

// ══════════════════════════════════════════════════════════════════════════════
// 槽函数
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::onPlayPause()
{
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer) {
        if (m_sdkPlayer && m_sdkPlaying) {
            XPlayerPause(m_sdkPlayer);
            m_sdkPlaying = false;
            if (m_sdkTimer) m_sdkTimer->stop();
            setPlayButtonState(false);
        } else if (m_sdkPlayer) {
            XPlayerStart(m_sdkPlayer);
            m_sdkPlaying = true;
            if (m_sdkTimer) m_sdkTimer->start();
            setPlayButtonState(true);
        } else {
            playMusic(m_currentIndex >= 0 ? m_currentIndex : 0);
        }
        return;
    }
#endif
    if (!m_mediaPlayer) return;
    if (m_mediaPlayer->state() == QMediaPlayer::PlayingState)
        m_mediaPlayer->pause();
    else if (m_mediaPlayer->state() == QMediaPlayer::PausedState)
        m_mediaPlayer->play();
    else
        playMusic(m_currentIndex >= 0 ? m_currentIndex : 0);
}

void MusicPlayerWindow::onNextMusic()
{
    if (m_musicFiles.isEmpty()) return;
    int next = (m_currentIndex + 1 >= m_musicFiles.count()) ? 0 : m_currentIndex + 1;
    playMusic(next);
}

void MusicPlayerWindow::onPreviousMusic()
{
    if (m_musicFiles.isEmpty()) return;
    int prev = (m_currentIndex <= 0) ? m_musicFiles.count() - 1 : m_currentIndex - 1;
    playMusic(prev);
}

void MusicPlayerWindow::onPlaylistItemClicked(QListWidgetItem *item)
{
    int row = m_playlistWidget->row(item);
    if (row >= 0 && row < m_musicFiles.count())
        playMusic(row);
}

void MusicPlayerWindow::onMusicListItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    QString itemPath = item->data(Qt::UserRole).toString();
    bool isDir = item->data(Qt::UserRole + 1).toBool();

    if (isDir) {
        // 进入子目录
        loadDirectory(itemPath);
    } else {
        // 播放文件：收藏页和目录页分别构建播放列表
        QStringList newPlaylist;
        int playIndex = 0;
        if (m_listFavMode) {
            newPlaylist = m_favoriteFiles;
            for (int i = 0; i < newPlaylist.count(); ++i) {
                if (newPlaylist[i] == itemPath) {
                    playIndex = i;
                    break;
                }
            }
        } else {
            QDir dir(m_currentBrowsePath);
            dir.setFilter(QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            dir.setSorting(QDir::Name);
            for (const QFileInfo &fi : dir.entryInfoList()) {
                if (m_audioExtensions.contains(fi.suffix().toLower())) {
                    if (fi.absoluteFilePath() == itemPath)
                        playIndex = newPlaylist.count();
                    newPlaylist << fi.absoluteFilePath();
                }
            }
            if (newPlaylist.isEmpty()) {
                newPlaylist << itemPath;
                playIndex = 0;
            }
        }
        m_musicFiles = newPlaylist;
        refreshPlaylistWidget();

        // 切换到播放页，然后播放
        m_stackedWidget->setCurrentIndex(kPagePlayer);
        playMusic(playIndex);
    }
}

void MusicPlayerWindow::onUsbTabClicked()
{
    if (m_isUsbMode) return;
    m_isUsbMode = true;
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }");
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }");
    scanFlatPlaylist();
    refreshPlaylistWidget();
}

void MusicPlayerWindow::onBtTabClicked()
{
    if (!m_isUsbMode) return;
    m_isUsbMode = false;
    releaseAudioPlayer();
    m_usbTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }");
    m_btTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }");
    m_musicFiles.clear();
    refreshPlaylistWidget();
    m_nowPlayingLabel->setText("蓝牙音乐");
    updateProgressBar(0, 0);
    setPlayButtonState(false);
}

void MusicPlayerWindow::onRescan()
{
    releaseAudioPlayer();
    m_currentIndex = -1;
    updateNowPlaying();
    updateProgressBar(0, 0);
    setPlayButtonState(false);
    scanFlatPlaylist();
    refreshPlaylistWidget();
}

void MusicPlayerWindow::onOpenListPage()
{
    // 打开列表页时，根据当前 tab 决定显示本地目录或收藏列表
    if (m_listFavMode)
        refreshFavoriteList();
    else
        loadDirectory(m_currentBrowsePath);
    m_stackedWidget->setCurrentIndex(kPageList);
}

void MusicPlayerWindow::onBackFromListPage()
{
    // 如果当前是收藏页，直接返回播放页；否则在目录页返回上一级
    if (m_listFavMode) {
        m_stackedWidget->setCurrentIndex(kPagePlayer);
        return;
    }

    QDir dir(m_currentBrowsePath);
    if (dir.cdUp() && dir.absolutePath() != m_currentBrowsePath &&
        m_currentBrowsePath != "/mnt" && m_currentBrowsePath != "/") {
        loadDirectory(dir.absolutePath());
    } else {
        m_currentBrowsePath = "/mnt";
        m_stackedWidget->setCurrentIndex(kPagePlayer);
    }
}

void MusicPlayerWindow::onListSongsTabClicked()
{
    m_listFavMode = false;
    m_listSongsTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_on.png); color: #fff; font-size: 28px; }");
    m_listFavTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_down.png); color: #fff; font-size: 28px; }");
    loadDirectory(m_currentBrowsePath);
}

void MusicPlayerWindow::onListFavTabClicked()
{
    m_listFavMode = true;
    m_listFavTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_left_on.png); color: #fff; font-size: 28px; }");
    m_listSongsTab->setStyleSheet(
        "QPushButton { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }"
        "QPushButton:pressed { border: none; background: url(:/images/butt_tab_right_down.png); color: #fff; font-size: 28px; }");
    refreshFavoriteList();
}

// ══════════════════════════════════════════════════════════════════════════════
// 辅助函数
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::updateNowPlaying()
{
    if (!m_nowPlayingLabel) return;
    if (m_currentIndex >= 0 && m_currentIndex < m_musicFiles.count())
        m_nowPlayingLabel->setText(QFileInfo(m_musicFiles[m_currentIndex]).baseName());
    else
        m_nowPlayingLabel->setText("未播放");
}

void MusicPlayerWindow::updateProgressBar(qint64 posMs, qint64 durMs)
{
    if (m_posLabel) m_posLabel->setText(formatTime(posMs));
    if (m_durLabel) m_durLabel->setText(formatTime(durMs));
    if (m_progressSlider && !m_sliderDragging) {
        if (durMs > 0)
            m_progressSlider->setValue(static_cast<int>((posMs * 1000) / durMs));
        else
            m_progressSlider->setValue(0);
    }
}

int MusicPlayerWindow::sliderValueFromMousePos(const QPoint &pos) const
{
    if (!m_progressSlider) return 0;
    int x = pos.x();
    int min = m_progressSlider->minimum();
    int max = m_progressSlider->maximum();
    int width = m_progressSlider->width();
    if (width <= 0) return min;
    int value = QStyle::sliderValueFromPosition(min, max, x, width, false);
    return qBound(min, value, max);
}

void MusicPlayerWindow::beginSliderSeek(int value)
{
    if (m_sliderDragging) return;
    m_sliderDragging = true;
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkPlayer) {
        m_wasPlayingBeforeSeek = m_sdkPlaying;
        if (m_sdkPlaying) {
            XPlayerPause(m_sdkPlayer);
            m_sdkPlaying = false;
            setPlayButtonState(false);
        }
        previewSliderSeek(value);
        return;
    }
#endif
    if (m_mediaPlayer) {
        m_wasPlayingBeforeSeek = (m_mediaPlayer->state() == QMediaPlayer::PlayingState);
        if (m_wasPlayingBeforeSeek) {
            m_mediaPlayer->pause();
        }
    }
    previewSliderSeek(value);
}

void MusicPlayerWindow::previewSliderSeek(int value)
{
    if (!m_progressSlider) return;
    m_progressSlider->setValue(value);
#ifdef CAR_DESK_USE_T507_SDK
    if (m_useSdkPlayer && m_sdkDurationMs > 0) {
        qint64 positionMs = (static_cast<qint64>(value) * m_sdkDurationMs) / 1000;
        updateProgressBar(positionMs, m_sdkDurationMs);
        return;
    }
#endif
    if (!m_mediaPlayer) return;
    qint64 durationMs = m_mediaPlayer->duration();
    if (durationMs > 0) {
        qint64 positionMs = (static_cast<qint64>(value) * durationMs) / 1000;
        updateProgressBar(positionMs, durationMs);
    }
}

void MusicPlayerWindow::finalizeSliderSeek(int value)
{
    if (!m_sliderDragging) return;
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
            if (m_sdkTimer && !m_sdkTimer->isActive()) m_sdkTimer->start();
        }
        m_wasPlayingBeforeSeek = false;
        return;
    }
#endif
    if (!m_mediaPlayer) return;
    qint64 durationMs = m_mediaPlayer->duration();
    if (durationMs > 0) {
        qint64 position = (static_cast<qint64>(value) * durationMs) / 1000;
        m_mediaPlayer->setPosition(position);
    }
    if (m_wasPlayingBeforeSeek && m_mediaPlayer && !m_mediaPlayer->media().isNull()) {
        m_mediaPlayer->play();
    }
    m_wasPlayingBeforeSeek = false;
}

void MusicPlayerWindow::processSliderRelease(int value)
{
    if (m_sliderDragging)
        finalizeSliderSeek(value);
}

void MusicPlayerWindow::setPlayButtonState(bool playing)
{
    if (!m_playButton) return;
    m_playButton->setStyleSheet(playing
        ? "QPushButton { border: none; background-image: url(:/images/butt_music_stop_up.png); background-repeat: no-repeat; }"
          "QPushButton:pressed { background-image: url(:/images/butt_music_stop_down.png); }"
        : "QPushButton { border: none; background-image: url(:/images/butt_music_play_up.png); background-repeat: no-repeat; }"
          "QPushButton:pressed { background-image: url(:/images/butt_music_play_down.png); }");
}

QString MusicPlayerWindow::formatTime(qint64 ms)
{
    int totalSec = static_cast<int>(ms / 1000);
    return QString("%1:%2")
        .arg(totalSec / 60, 2, 10, QChar('0'))
        .arg(totalSec % 60, 2, 10, QChar('0'));
}

// ══════════════════════════════════════════════════════════════════════════════
// QMediaPlayer 回调（PC 端）
// ══════════════════════════════════════════════════════════════════════════════

void MusicPlayerWindow::onMediaPositionChanged(qint64 position)
{
    if (!m_sliderDragging)
        updateProgressBar(position, m_mediaPlayer->duration());
}

void MusicPlayerWindow::onMediaDurationChanged(qint64 duration)
{
    updateProgressBar(m_mediaPlayer->position(), duration);
}

void MusicPlayerWindow::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia) {
        if (m_musicFiles.isEmpty()) {
            setPlayButtonState(false);
            return;
        }
        int next = (m_currentIndex + 1 >= m_musicFiles.count()) ? 0 : m_currentIndex + 1;
        playMusic(next);
    }
}

void MusicPlayerWindow::onMediaStateChanged(QMediaPlayer::State state)
{
    setPlayButtonState(state == QMediaPlayer::PlayingState);
}

// ══════════════════════════════════════════════════════════════════════════════
// T507 SDK 回调
// ══════════════════════════════════════════════════════════════════════════════

#ifdef CAR_DESK_USE_T507_SDK

void MusicPlayerWindow::onSdkTick()
{
    if (!m_sdkPlayer || !m_sdkPlaying) return;
    int posMs = 0;
    if (XPlayerGetCurrentPosition(m_sdkPlayer, &posMs) == 0)
        updateProgressBar(posMs, m_sdkDurationMs);
}

void MusicPlayerWindow::onSdkPlaybackComplete()
{
    if (m_sdkSwitching) return;
    m_sdkPlaying = false;
    if (m_sdkTimer) m_sdkTimer->stop();
    setPlayButtonState(false);

    if (m_musicFiles.isEmpty()) return;

    int next = (m_currentIndex < m_musicFiles.count() - 1) ? (m_currentIndex + 1) : 0;
    m_sdkSwitching = true;
    QTimer::singleShot(300, this, [this, next]() {
        m_sdkSwitching = false;
        playMusic(next);
    });
}

#endif // CAR_DESK_USE_T507_SDK

void MusicPlayerWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, this);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, this);
        break;
    case Qt::Key_HomePage:
        emit requestReturnToMain();
        close();
        break;
    case Qt::Key_Back:
    case Qt::Key_Escape:
        if (m_stackedWidget && m_stackedWidget->currentIndex() == kPageList) {
            onBackFromListPage();
        } else {
            emit requestReturnToMain();
            close();
        }
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
