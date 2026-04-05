#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QPushButton>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>

class BluetoothManager;
class MediaManager;
class PhoneWindow;
class RadioWindow;
class DiagnosticWindow;
class SystemSettingWindow;
class DrivingImageWindow;
class ImageViewingWindow;
class USBManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUpdateClock();
    void onBluetoothClicked();
    void onUSBClicked();
    void onVolumeClicked();
    void onVideoListClicked();
    void onMusicUSBClicked();
    void onPhoneClicked();
    void onRadioClicked();
    void onDiagnosticClicked();
    void onSystemSettingsClicked();
    void onDrivingImageClicked();
    void onImageViewingClicked();

protected:
    void closeEvent(QCloseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUI();
    void setupConnections();
    void adjustForDevice();
    void createTopBar();
    void createNavigationBar();
    void setupWindowSize();
    void setupSystemInfo();
    void applyIndexStyle();
    void forceMainInterfaceRedraw();
    void ensureTransitionOverlay();
    void showTransitionOverlay();
    void hideTransitionOverlay();
    
    // UI 组件
    QLabel *m_clockLabel;
    QLabel *m_volumeLabel;
    QPushButton *m_volBtn;
    bool m_isMuted;
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
    USBManager *m_usbManager;
    
    // 定时器
    QTimer *m_clockTimer;
    
    // 样式表
    QString m_styleSheet;
};

#endif // MAINWINDOW_H
