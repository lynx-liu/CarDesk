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

private:
    void setupUI();
    void loadVideoFiles();
    void scanVideoDirectories();
    void updateTitle();

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
};

#endif // VIDEOPLAYWINDOW_H
