#include "mediamanager.h"
#include "videolistwindow.h"
#include "videoplaywindow.h"
#include "musicplayerwindow.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
    , m_isPlaying(false)
    , m_videoListWindow(nullptr)
    , m_videoPlayWindow(nullptr)
    , m_musicWindow(nullptr)
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
        m_videoListWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_videoListWindow, &QObject::destroyed, this, [this]() {
            m_videoListWindow = nullptr;
        });
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

void MediaManager::openMusicPlayer() {
    qDebug() << "Opening music player...";
    
    if (!m_musicWindow) {
        m_musicWindow = new MusicPlayerWindow();
        m_musicWindow->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_musicWindow, &QObject::destroyed, this, [this]() {
            m_musicWindow = nullptr;
        });
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
