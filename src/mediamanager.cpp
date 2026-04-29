#include "mediamanager.h"
#include "bluetoothmanager.h"
#include "videolistwindow.h"
#include "videoplaywindow.h"
#include "musicplayerwindow.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

#include "t507sdkbridge.h"
#include "radiowindow.h"

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
    , m_isPlaying(false)
    , m_videoListWindow(nullptr)
    , m_videoPlayWindow(nullptr)
    , m_musicWindow(nullptr)
    , m_bluetoothManager(nullptr)
    , m_radioWindow(nullptr)
{
}

MediaManager::~MediaManager() {
    if (m_videoListWindow) delete m_videoListWindow;
    if (m_videoPlayWindow) delete m_videoPlayWindow;
    if (m_musicWindow) delete m_musicWindow;
}

void MediaManager::openVideoList() {
    qDebug() << "Opening video list...";
    
    if (!m_videoListWindow) {
        m_videoListWindow = new VideoListWindow();
        if (m_bluetoothManager) {
            m_videoListWindow->setBluetoothManager(m_bluetoothManager);
        }
        m_videoListWindow->setMusicWindow(m_musicWindow);
        m_videoListWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_videoListWindow, &QObject::destroyed, this, [this]() {
            m_videoListWindow = nullptr;
        });
    } else if (m_videoListWindow) {
        m_videoListWindow->setMusicWindow(m_musicWindow);
    }
    // 若视频播放器处于 HOME 键暂停状态，直接恢复播放，跳过文件列表界面
    // 避免在 showEvent 内 hide() 后再 raise() 引发的窗口状态混乱
    if (m_videoListWindow->tryResumeVideo()) {
        return;
    }
    m_videoListWindow->showNormal();
    m_videoListWindow->raise();
    m_videoListWindow->activateWindow();
}

VideoListWindow *MediaManager::videoListWindow() const {
    return m_videoListWindow;
}

MusicPlayerWindow *MediaManager::musicWindow() const {
    return m_musicWindow;
}

void MediaManager::setBluetoothManager(BluetoothManager *manager) {
    m_bluetoothManager = manager;
    if (m_musicWindow) {
        m_musicWindow->setBluetoothManager(manager);
    }
}

void MediaManager::setRadioWindow(RadioWindow *window) {
    m_radioWindow = window;
}

RadioWindow *MediaManager::radioWindow() const {
    return m_radioWindow;
}

void MediaManager::prepareForBluetoothMusic() {
    qDebug() << "MediaManager: prepareForBluetoothMusic";
    if (m_radioWindow) {
        m_radioWindow->forceStopAudio();
    }
    if (m_musicWindow) {
        m_musicWindow->stopIfPlaying();
    }
    if (m_videoListWindow) {
        m_videoListWindow->pauseVideoIfPlaying();
    }
    if (m_bluetoothManager) {
        m_bluetoothManager->setPlaybackMode(1);
    }
    T507SdkBridge::setAudioSource(false);
}

void MediaManager::prepareForNonBluetoothAudio() {
    qDebug() << "MediaManager: prepareForNonBluetoothAudio";
    if (m_radioWindow) {
        m_radioWindow->forceStopAudio();
    }
    if (m_bluetoothManager) {
        m_bluetoothManager->setPlaybackMode(0);
        m_bluetoothManager->stopMusic();
    }
    T507SdkBridge::setAudioSource(false);
}

void MediaManager::prepareForRadioAudio() {
    qDebug() << "MediaManager: prepareForRadioAudio";
    if (m_musicWindow) {
        m_musicWindow->pauseIfPlaying();
    }
    if (m_videoListWindow) {
        m_videoListWindow->pauseVideoIfPlaying();
    }
    if (m_bluetoothManager) {
        m_bluetoothManager->setPlaybackMode(0);
        m_bluetoothManager->stopMusic();
    }
    T507SdkBridge::setAudioSource(true);
}

void MediaManager::openMusicPlayer() {
    qDebug() << "Opening music player...";

    if (!m_musicWindow) {
        // 不使用 WA_DeleteOnClose：窗口复用，避免析构时 XPlayerReset 时序问题
        m_musicWindow = new MusicPlayerWindow();
        if (m_bluetoothManager) {
            m_musicWindow->setBluetoothManager(m_bluetoothManager);
        }
        m_musicWindow->setMediaManager(this);
        if (m_videoListWindow) {
            m_videoListWindow->setMusicWindow(m_musicWindow);
        }
    }
    m_musicWindow->showNormal();
    m_musicWindow->raise();
    m_musicWindow->activateWindow();
}

void MediaManager::playMedia(const QString &filePath) {
    qDebug() << "Playing media:" << filePath;
    
    m_currentMediaFile = filePath;
    m_isPlaying = true;
    
    // TODO: 使用 QMediaPlayer 或系统播放器播放媒体
    
    emit mediaOpened(filePath);
    emit playbackStarted();
}

void MediaManager::stopPlayback() {
    if (!m_isPlaying) return;
    
    qDebug() << "Stopping playback";
    m_isPlaying = false;
    emit playbackStopped();
}

void MediaManager::pausePlayback() {
    if (!m_isPlaying) return;
    
    qDebug() << "Pausing playback";
    m_isPlaying = false;
    emit playbackPaused();
}

void MediaManager::resumePlayback() {
    if (m_isPlaying) return;
    
    qDebug() << "Resuming playback";
    m_isPlaying = true;
}

QStringList MediaManager::getVideoFiles(const QString &directory) {
    QStringList videoFiles;
    
    QString searchDir = directory.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : 
        directory;
    
    QDir dir(searchDir);
    dir.setFilter(QDir::Files);
    dir.setNameFilters({"*.mp4", "*.avi", "*.mkv", "*.mov", "*.flv"});
    
    videoFiles = dir.entryList();
    
    qDebug() << "Found" << videoFiles.count() << "video files";
    
    return videoFiles;
}

QStringList MediaManager::getAudioFiles(const QString &directory) {
    QStringList audioFiles;
    
    QString searchDir = directory.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation) : 
        directory;
    
    QDir dir(searchDir);
    dir.setFilter(QDir::Files);
    dir.setNameFilters({"*.mp3", "*.flac", "*.wav", "*.aac", "*.ogg"});
    
    audioFiles = dir.entryList();
    
    qDebug() << "Found" << audioFiles.count() << "audio files";
    
    return audioFiles;
}
