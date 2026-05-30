#ifndef DRIVINGIMAGEPLAYBACKPAGE_H
#define DRIVINGIMAGEPLAYBACKPAGE_H

#include <QWidget>

class QGridLayout;
class QLabel;
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
    void populateDateGrid();
    void populateFileGrid();
    void updatePageButtons();

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
    VideoPlayWindow *m_player = nullptr;
    QString m_currentDate;
    QStringList m_allDates;
    QStringList m_allFiles;
    int m_datePageIndex = 0;
    int m_currentPage = 0;
    bool m_showingFiles = false;
};

#endif // DRIVINGIMAGEPLAYBACKPAGE_H
