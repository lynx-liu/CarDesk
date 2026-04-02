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
    
    void setVideoFiles(const QStringList &files, int currentIndex = 0);
    void setCurrentVideo(const QString &filePath);

signals:
    void requestReturnToList();

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

private:
    void setupUI();
    void loadVideoFiles();
    void scanVideoDirectories();
    void updateTitle();
    void setPlayButtonState(bool playing);
    void updateTimeAndSlider(qint64 positionMs, qint64 durationMs);

#ifdef CAR_DESK_USE_T507_SDK
    bool initSdkPlayer(const QString &videoPath);
    void releaseSdkPlayer();
#endif

    QLabel *m_titleLabel;
    QLabel *m_timeLabel;
    QLabel *m_durationLabel;
    QPushButton *m_prevButton;
    QPushButton *m_playButton;
    QPushButton *m_nextButton;
    QPushButton *m_backButton;
    QSlider *m_progressSlider;
    
    int m_currentIndex;
    QStringList m_videoFiles;
    QProcess *m_playerProcess;
    QTimer *m_hideTimer;
    QMediaPlayer *m_mediaPlayer;
    QVideoWidget *m_videoWidget;
    bool m_useSdkPlayer;

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
#endif
};

#endif // VIDEOPLAYWINDOW_H
