#ifndef MUSICPLAYERWINDOW_H
#define MUSICPLAYERWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QCloseEvent>

class MusicPlayerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MusicPlayerWindow(QWidget *parent = nullptr);
    ~MusicPlayerWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void requestReturnToMain();

private slots:
    void onPlayMusic();
    void onStopMusic();
    void onNextMusic();
    void onPreviousMusic();
    void onListClicked();
    void onUsbTabClicked();
    void onBtTabClicked();
    void onGoBack();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void setupUI();
    void loadMusicFiles();
    void scanMusicDirectories();
    void updateNowPlaying();

    QLabel *m_titleLabel;
    QLabel *m_nowPlayingLabel;
    QListWidget *m_playlistWidget;
    QPushButton *m_homeButton;
    QPushButton *m_usbTab;
    QPushButton *m_btTab;
    QPushButton *m_listButton;
    QPushButton *m_prevButton;
    QPushButton *m_playButton;
    QPushButton *m_nextButton;
    QPushButton *m_loopButton;
    
    int m_currentIndex;
    QStringList m_musicFiles;
    QProcess *m_playerProcess;
    bool m_isUsbMode;
};

#endif // MUSICPLAYERWINDOW_H
