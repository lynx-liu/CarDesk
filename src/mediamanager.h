#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class VideoListWindow;
class VideoPlayWindow;
class MusicPlayerWindow;

class MediaManager : public QObject {
    Q_OBJECT

public:
    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager();
    
    void openVideoList();
    void openMusicPlayer();
    void playMedia(const QString &filePath);
    void stopPlayback();
    void pausePlayback();
    void resumePlayback();

    VideoListWindow *videoListWindow() const;
    MusicPlayerWindow *musicWindow() const;
    
    QStringList getVideoFiles(const QString &directory = "");
    QStringList getAudioFiles(const QString &directory = "");

signals:
    void mediaOpened(const QString &filePath);
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();
    void playbackFinished();
    void error(const QString &errorMsg);

private:
    QString m_currentMediaFile;
    bool m_isPlaying;
    VideoListWindow *m_videoListWindow;
    VideoPlayWindow *m_videoPlayWindow;
    MusicPlayerWindow *m_musicWindow;
};

#endif // MEDIAMANAGER_H
