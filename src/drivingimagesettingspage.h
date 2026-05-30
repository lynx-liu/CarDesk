#ifndef DRIVINGIMAGESETTINGSPAGE_H
#define DRIVINGIMAGESETTINGSPAGE_H

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class DrivingImageSubTopBar;
class AhdManager;

class DrivingImageSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DrivingImageSettingsPage(AhdManager *manager, QWidget *parent = nullptr);

signals:
    void requestReturnToMain();
    void recordingToggled(bool enabled);

private:
    void setupUI();
    void refreshRecordingSwitch();
    void refreshModeTiles();
    void onFormatClicked();
    static bool showPopAlert(QWidget *parent, const QString &title, bool withCancel);

    AhdManager *m_manager = nullptr;
    DrivingImageSubTopBar *m_topBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_subnav = nullptr;
    QPushButton *m_recordingSwitch = nullptr;
    QPushButton *m_modeRear = nullptr;
    QPushButton *m_modeLeftRight = nullptr;
};

#endif // DRIVINGIMAGESETTINGSPAGE_H
