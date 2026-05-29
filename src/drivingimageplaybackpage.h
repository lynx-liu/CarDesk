#ifndef DRIVINGIMAGEPLAYBACKPAGE_H
#define DRIVINGIMAGEPLAYBACKPAGE_H

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class DrivingImageSubTopBar;
class VideoPlayWindow;

class DrivingImagePlaybackPage : public QWidget {
    Q_OBJECT

public:
    explicit DrivingImagePlaybackPage(QWidget *parent = nullptr);
    ~DrivingImagePlaybackPage() override;

    void reloadDates();

signals:
    void requestReturnToMain();
    void requestReturnToPreview();

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void setupUI();
    void showDateList();
    void showFileList(const QString &dateKey);
    void playFile(const QString &path);

    DrivingImageSubTopBar *m_topBar = nullptr;
    QPushButton *m_backBtn = nullptr;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_dateList = nullptr;
    QListWidget *m_fileList = nullptr;
    QLabel *m_emptyHint = nullptr;
    QPushButton *m_prevPageBtn = nullptr;
    QPushButton *m_nextPageBtn = nullptr;
    VideoPlayWindow *m_player = nullptr;
    QString m_currentDate;
    int m_datePageIndex = 0;
};

#endif // DRIVINGIMAGEPLAYBACKPAGE_H
