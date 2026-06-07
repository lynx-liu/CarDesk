#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QPushButton>
#include <QCloseEvent>
#include <QHideEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QPointer>
#include <QVector>
#include <QStringList>
#include <functional>

class BluetoothManager;
class MediaManager;
class PhoneWindow;
class RadioWindow;
class DiagnosticWindow;
class SystemSettingWindow;
class DrivingImageWindow;
class ImageViewingWindow;
class VideoListWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    MediaManager *mediaManager() const;

    void showDrivingImageForAutomotive(int mode);

private slots:
    void onBluetoothClicked();
    void onVideoListClicked();
    void onMusicUSBClicked();
    void onPhoneClicked();
    void onRadioClicked();
    void onDiagnosticClicked();
    void onSystemSettingsClicked();
    void onDrivingImageClicked();
    void onImageViewingClicked();
    void onDrivingRecordPlayRequested(const QStringList &files, int currentIndex);
    void onBluetoothCallStatusChanged(int status);

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUI();
    void setupConnections();
    void adjustForDevice();
    void createTopBar();
    void createNavigationBar();
    void setupWindowSize();
    void setupSystemInfo();
    void applyIndexStyle();
    void ensureTransitionOverlay();
    void showTransitionOverlay();
    void hideTransitionOverlay();
    void ensurePhoneWindow();
    void ensureRadioWindow();
    void ensureDiagnosticWindow();
    void ensureSystemSettingWindow();
    void ensureImageViewingWindow();
    void ensureDrivingImageWindow();
    void connectVideoListReturnToMain(VideoListWindow *listWindow);
    void openVideoPlayback(const QStringList &files, int currentIndex,
                           const std::function<void()> &returnToList = {});
    QWidget *findCurrentVisibleNonPhoneWindow() const;
    void restorePreviousWindow();

    // UI 组件
    QWidget *m_topBar;
    QWidget *m_navBar;
    QWidget *m_centralWidget;
    QWidget *m_transitionOverlay;
    QWidget *m_volumeWidget;
    
    // 业务对象
    BluetoothManager *m_bluetoothManager;
    MediaManager *m_mediaManager;
    PhoneWindow *m_phoneWindow;
    RadioWindow *m_radioWindow;
    DiagnosticWindow *m_diagnosticWindow;
    SystemSettingWindow *m_systemSettingWindow;
    DrivingImageWindow *m_drivingImageWindow;
    ImageViewingWindow *m_imageViewingWindow;
    struct RestoreState {
        QPointer<QWidget> widget;
    };

    QVector<RestoreState> m_restoreStack;

    // 样式表
    QString m_styleSheet;
};

#endif // MAINWINDOW_H
