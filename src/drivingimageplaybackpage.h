#ifndef DRIVINGIMAGEPLAYBACKPAGE_H
#define DRIVINGIMAGEPLAYBACKPAGE_H

#include <QWidget>
#include <QStringList>

class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QShowEvent;
class QTimer;
class DrivingImageSubTopBar;

class DrivingImagePlaybackPage : public QWidget {
    Q_OBJECT

public:
    explicit DrivingImagePlaybackPage(QWidget *parent = nullptr);

    void reloadDates();
    /** 从录像播放器返回：有当前日期则回到该日期的文件列表，否则刷新日期列表 */
    void restoreAfterVideoPlayback();

signals:
    void requestReturnToMain();
    void requestReturnToPreview();
    void requestPlayVideo(const QStringList &files, int currentIndex);

private:
    void setupUI();
    void showDateList();
    void showFileList(const QString &dateKey);
    void playFile(const QString &path);
    void populateDateGrid();
    void populateFileGrid();
    void updatePageButtons();
    void refreshCurrentView();
    void showEvent(QShowEvent *event) override;

    DrivingImageSubTopBar *m_topBar = nullptr;
    QPushButton *m_backBtn = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_dateGridHost = nullptr;
    QWidget *m_fileGridHost = nullptr;
    QGridLayout *m_dateGrid = nullptr;
    QGridLayout *m_fileGrid = nullptr;
    QLabel *m_emptyHint = nullptr;
    QPushButton *m_prevPageBtn = nullptr;
    QPushButton *m_nextPageBtn = nullptr;
    QString m_currentDate;
    QStringList m_allDates;
    QStringList m_allFiles;
    int m_datePageIndex = 0;
    int m_currentPage = 0;
    bool m_showingFiles = false;
    bool m_launchingPlayback = false;
    QTimer *m_recordRefreshDebounce = nullptr;
};

#endif // DRIVINGIMAGEPLAYBACKPAGE_H
