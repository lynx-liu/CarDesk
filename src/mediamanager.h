#ifndef MEDIAMANAGER_H
#define MEDIAMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class VideoListWindow;
class VideoPlayWindow;
class MusicPlayerWindow;
class RadioWindow;

class BluetoothManager;

class MediaManager : public QObject {
    Q_OBJECT

public:
    enum class AudioSource {
        None,
        Radio,
        Media,
        BluetoothMusic
    };

    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager();
    
    void setBluetoothManager(BluetoothManager *manager);
    void setRadioWindow(RadioWindow *window);
    void openVideoList();
    void openMusicPlayer();
    void prepareForBluetoothMusic();
    void prepareForNonBluetoothAudio();
    void prepareForRadioAudio();
    void playMedia(const QString &filePath);
    void stopPlayback();
    void pausePlayback();
    void resumePlayback();

    VideoListWindow *videoListWindow() const;
    MusicPlayerWindow *musicWindow() const;
    RadioWindow *radioWindow() const;
    AudioSource currentAudioSource() const;
    void setCurrentAudioSource(AudioSource source);
    
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
    AudioSource m_currentAudioSource;
    VideoListWindow *m_videoListWindow;
    VideoPlayWindow *m_videoPlayWindow;
    MusicPlayerWindow *m_musicWindow;
    BluetoothManager *m_bluetoothManager;
    RadioWindow *m_radioWindow;
};

#endif // MEDIAMANAGER_H
