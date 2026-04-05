#ifndef MUSICPLAYERWINDOW_H
#define MUSICPLAYERWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QMediaPlayer>
#include <QCloseEvent>
#include <QStyledItemDelegate>
#include <QPainter>

#ifdef CAR_DESK_USE_T507_SDK
#include <xplayer.h>
#include <soundControl_tinyalsa.h>
#endif

// ── 播放页底部水平播放列表委托（120×120，左右各 12px 外边距）──
class MusicPlaylistDelegate : public QStyledItemDelegate {
public:
    explicit MusicPlaylistDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(144, 120);  // 120 item + 24 total margin
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save();
        QRect r(option.rect.x() + 12, option.rect.y(), 120, 120);

        static QPixmap bg(":/images/music_play_musiclist_show.png");
        if (!bg.isNull()) p->drawPixmap(r, bg);

        bool selected = option.state & QStyle::State_Selected;
        QPen pen(selected ? QColor("#00FAFF") : QColor("#0068FF"), selected ? 2 : 1);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(r.adjusted(0, 0, -1, -1));

        // 底部半透明文字区（margin-top:88px, h:30px）
        QString text = index.data(Qt::DisplayRole).toString();
        QRect textBg(r.x(), r.y() + 88, 120, 30);
        p->fillRect(textBg, QColor(0, 0, 0, 191));
        p->setPen(Qt::white);
        QFont font;
        font.setPixelSize(20);
        p->setFont(font);
        p->drawText(textBg, Qt::AlignHCenter | Qt::AlignVCenter, text);

        p->restore();
    }
};

// ── 音乐列表网格委托（160×160，与视频列表一致）──
class MusicListItemDelegate : public QStyledItemDelegate {
public:
    explicit MusicListItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(188, 178);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save();
        QRect r(option.rect.x(), option.rect.y(), 160, 160);

        bool isDir = index.data(Qt::UserRole + 1).toBool();
        static QPixmap fileUp  (":/images/music_musiclist_up.png");
        static QPixmap fileDown(":/images/music_musiclist_down.png");
        static QPixmap folderUp  (":/images/music_musiclist_file_up.png");
        static QPixmap folderDown(":/images/music_musiclist_file_down.png");

        bool hovered = option.state & (QStyle::State_MouseOver | QStyle::State_Selected);
        const QPixmap &px = isDir ? (hovered ? folderDown : folderUp)
                                  : (hovered ? fileDown   : fileUp);
        if (!px.isNull()) p->drawPixmap(r, px);

        QString text = index.data(Qt::DisplayRole).toString();
        QRect textRect(r.x(), r.y() + 117, 160, 42);
        p->setPen(Qt::white);
        QFont font;
        font.setPixelSize(20);
        p->setFont(font);
        p->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, text);

        p->restore();
    }
};

// ────────────────────────────────────────────────────────────────────────────

class MusicPlayerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MusicPlayerWindow(QWidget *parent = nullptr);
    ~MusicPlayerWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void requestReturnToMain();

private slots:
    void onPlayPause();
    void onNextMusic();
    void onPreviousMusic();
    void onPlaylistItemClicked(QListWidgetItem *item);   // 底部水平列表
    void onMusicListItemClicked(QListWidgetItem *item);  // 列表页网格
    void onUsbTabClicked();
    void onBtTabClicked();
    void onRescan();
    void onOpenListPage();
    void onBackFromListPage();
    void onListSongsTabClicked();
    void onListFavTabClicked();

    void onMediaPositionChanged(qint64 position);
    void onMediaDurationChanged(qint64 duration);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onMediaStateChanged(QMediaPlayer::State state);

#ifdef CAR_DESK_USE_T507_SDK
    void onSdkTick();
    void onSdkPlaybackComplete();
#endif

private:
    void setupUI();
    void setupPlayerPage(QWidget *page);
    void setupListPage(QWidget *page);
    void loadDirectory(const QString &path);
    void scanFlatPlaylist();
    void playMusic(int index);
    void releaseAudioPlayer();
    void updateNowPlaying();
    void updateProgressBar(qint64 posMs, qint64 durMs);
    void setPlayButtonState(bool playing);
    void refreshPlaylistWidget();
    static QString formatTime(qint64 ms);

    // ── Stacked pages ──
    QStackedWidget *m_stackedWidget = nullptr;
    static constexpr int kPagePlayer = 0;
    static constexpr int kPageList   = 1;

    // ── 播放页控件（绝对坐标，匹配 music_usb_play.html）──
    QLabel      *m_titleLabel     = nullptr;
    QLabel      *m_nowPlayingLabel = nullptr;
    QPushButton *m_homeButton     = nullptr;
    QPushButton *m_usbTab         = nullptr;
    QPushButton *m_btTab          = nullptr;
    QPushButton *m_listButton     = nullptr;
    QPushButton *m_prevButton     = nullptr;
    QPushButton *m_playButton     = nullptr;
    QPushButton *m_nextButton     = nullptr;
    QPushButton *m_loopButton     = nullptr;
    QLabel      *m_posLabel       = nullptr;
    QLabel      *m_durLabel       = nullptr;
    QSlider     *m_progressSlider = nullptr;
    QListWidget *m_playlistWidget = nullptr;  // 底部水平列表

    // ── 列表页控件（匹配 music_usb_play_list.html）──
    QListWidget *m_musicListWidget    = nullptr;
    QPushButton *m_backFromListButton = nullptr;
    QLabel      *m_listPathLabel      = nullptr;
    QPushButton *m_listSongsTab       = nullptr;
    QPushButton *m_listFavTab         = nullptr;

    // ── 状态 ──
    int         m_currentIndex       = -1;
    QStringList m_musicFiles;                    // 当前播放列表（平铺）
    bool        m_isUsbMode          = true;
    QString     m_currentBrowsePath;             // 列表页当前浏览路径
    const QStringList m_audioExtensions = {
        "mp3","flac","wav","aac","ogg","wma","opus","m4a","ape","ac3"
    };

    // ── PC 端 ──
    QMediaPlayer *m_mediaPlayer = nullptr;

#ifdef CAR_DESK_USE_T507_SDK
    XPlayer   *m_sdkPlayer     = nullptr;
    SoundCtrl *m_sdkSoundCtrl  = nullptr;
    QTimer    *m_sdkTimer      = nullptr;
    bool       m_sdkPlaying    = false;
    bool       m_sdkSwitching  = false;
    int        m_sdkDurationMs = 0;
    bool       m_useSdkPlayer  = false;
#endif
};

#endif // MUSICPLAYERWINDOW_H
