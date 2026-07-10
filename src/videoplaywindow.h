#ifndef VIDEOPLAYWINDOW_H
#define VIDEOPLAYWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QSlider>
#include <QTimer>
#include <QMediaPlayer>
#include <QVideoWidget>

class BluetoothManager;

#include "videoplaybackorigin.h"

#ifdef CAR_DESK_USE_T507_SDK
#include <xplayer.h>
#include <outputCtrl.h>
#include <soundControl_tinyalsa.h>
#endif

class VideoPlayWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit VideoPlayWindow(QWidget *parent = nullptr);
    ~VideoPlayWindow();
    
    void setVideoFiles(const QStringList &files, int currentIndex = 0,
                       VideoPlaybackOrigin origin = VideoPlaybackOrigin::UsbVideoList);
    void setCurrentVideo(const QString &filePath);
    void setBluetoothManager(BluetoothManager *manager);
    bool isPausedForHome() const { return m_pausedForHome; }
    bool hasPendingResume() const { return m_pausedForHome || m_pausedForOcclusion || m_pausedForInterruption; }
    bool hasPendingResumeFor(VideoPlaybackOrigin origin) const;
    VideoPlaybackOrigin playbackOrigin() const { return m_playbackOrigin; }
    QString currentVideoPath() const;
    bool isPlaying() const;
    void pauseIfPlaying();
    void pauseForInterruption();
    void resumeAfterInterruption();
    /** 拔出 U 盘时停止 USB 视频播放并释放播放器（不影响行车录像回放） */
    void stopUsbPlayback();

signals:
    void requestReturnToList();
    void requestReturnToMain();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onPlayVideo();
    void onNextVideo();
    void onPreviousVideo();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlaybackStateChanged(QMediaPlayer::State state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSdkTick();
    void onSdkPlaybackComplete();
    void onSdkSeekComplete();
    void continueSdkVideoSwitch();

private:
    void setupUI();
    void loadVideoFiles();
    void scanVideoDirectories();
    void updateTitle();
    void setPlayButtonState(bool playing);
    void updateTimeAndSlider(qint64 positionMs, qint64 durationMs);
    void resetInactivityTimer();
    void hideControls();
    void showControls();
    void handleUserActivity();
    void refreshControlsProgressUi();
    int sliderValueFromContainerPos(const QPoint &pos) const;
    void beginSliderSeek(int value);
    void previewSliderSeek(int value);
    void finalizeSliderSeek(int value);
    void refreshRecordPlaylistIfNeeded();
    void reloadDrivingRecordPlaylist();
    bool isDrivingRecordPlayback() const;
    bool selectAdjacentRecordFile(int direction);
    bool ensureCurrentFilePlayable();

#ifdef CAR_DESK_USE_T507_SDK
    bool initSdkPlayer(const QString &videoPath);
    bool startSdkPlayer(const QString &videoPath);
    void releaseSdkPlayer();
    void forceReleaseSdkPlayer();
    void resetSdkPlayerForCall();
    bool restoreSdkPlaybackAfterInterruption();
    void requestSdkVideoSwitch();
    void beginSdkVideoSwitch();
#endif

    QLabel *m_titleLabel;
    QLabel *m_timeLabel;
    QLabel *m_durationLabel;
    QPushButton *m_prevButton;
    QPushButton *m_playButton;
    QPushButton *m_nextButton;
    QPushButton *m_backButton;
    QSlider *m_progressSlider;
    QWidget *m_progressContainer;
    QWidget *m_topBar;
    QWidget *m_bottomBar;
    QLabel  *m_speedWarningLabel;
    
    int m_currentIndex;
    QStringList m_videoFiles;
    QProcess *m_playerProcess;
    QTimer *m_hideTimer;
    QMediaPlayer *m_mediaPlayer;
    QVideoWidget *m_videoWidget;
    bool m_useSdkPlayer;
    bool m_controlsHidden;
    bool m_sliderDragging;
    BluetoothManager *m_bluetoothManager;
    bool m_wasPlayingBeforeSeek;
    bool m_pausedForHome;      // HOME 键退出时置位，供 tryResumeVideo 判断
    bool m_pausedForOcclusion; // 其他窗口覆盖时暂停，恢复时继续播放
    bool m_pausedForInterruption; // 电话中断时保存播放状态，待恢复
    bool m_speedHighLocked;    // 车速>=10km/h时封锁播放按鈕
    QString m_resumePath;      // HOME 退出前的视频文件路径
    int m_resumePositionMs;    // HOME 退出前的播放位置（ms）
    int m_resumeInterruptPositionMs; // 其他中断时的视频恢复位置（ms）
    VideoPlaybackOrigin m_playbackOrigin = VideoPlaybackOrigin::None;
    QString m_recordDateKey;
    int m_switchDirection = 1;

#ifdef CAR_DESK_USE_T507_SDK
    XPlayer *m_sdkPlayer;
    SoundCtrl *m_sdkSoundCtrl;
    LayerCtrl *m_sdkLayerCtrl;
    SubCtrl *m_sdkSubCtrl;
    Deinterlace *m_sdkDi;
    QTimer *m_sdkTimer;
    qint64 m_sdkDurationMs;
    bool m_sdkPlaying;
    bool m_sdkSwitching;
    bool m_switchPending;  // 切歌中又收到下一首：只合并索引，不重叠 Reset/Prepare
    bool m_sdkSeeking;     // seek 进行中标志
    bool m_pendingRelease; // releaseSdkPlayer 在 seek 期间被推迟，待 SEEK_COMPLETE 后执行
#endif
};

#endif // VIDEOPLAYWINDOW_H
