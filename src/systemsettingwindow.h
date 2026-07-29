#ifndef SYSTEMSETTINGWINDOW_H
#define SYSTEMSETTINGWINDOW_H

#include <QMainWindow>
#include <QString>

class QLabel;
class QListWidget;
class QProgressBar;
class QStackedWidget;
class QTimer;
class QTabWidget;
class QPushButton;
class QWidget;
class OTAManager;

class BluetoothManager;

class SystemSettingWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SystemSettingWindow(BluetoothManager *bluetoothManager, QWidget *parent = nullptr);
    void setBluetoothManager(BluetoothManager *bluetoothManager);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSubnavChanged(int index);
    void onStartUpdate();
    void onCancelUpdate();
    void onTickUpdate();
    
    // OTA相关槽函数
    void onUpdateProgress(int percentage);
    void onUpdateStateChanged(const QString &state);
    void onUpdateStarted();
    void onUpdateCompleted();
    void onUpdateFailed(const QString &error);
    void onUpdateCancelled();
    void onFirmwareCheckUpdate();
    void onMcuUpdateStart();
    void onMcuUpdateCancel();
    void onMcuSerialReadyRead();

private:
    QString findAppUpdateArchive(QString *usbRoot = nullptr) const;
    void setupUI();
    QWidget *createDisplayPage();
    QWidget *createSoundPage();
    QWidget *createBluetoothPage();
    QWidget *createInfoPage();
    QWidget *createFactoryPage();
    QWidget *createUpdatePage();

    QStackedWidget *m_pages;
    QListWidget *m_subnavList;
    BluetoothManager *m_bluetoothManager;
    int m_bluetoothPageIndex;
    QLabel *m_bluetoothIntroLabel;
    QString m_bluetoothDeviceName;
    QString m_bluetoothPairPin;
    bool m_bluetoothEnabled;
    bool m_bluetoothPairedDevicesLoaded;
    QTimer *m_bluetoothConnectionTimer;

    // 应用升级相关
    QLabel *m_updateStateLabel;
    QLabel *m_updateProgressText;
    QProgressBar *m_updateProgressBar;
    QLabel *m_updateIntroLabel;
    QWidget *m_updateProgressRowWidget;
    QPushButton *m_updateStartBtn;
    QPushButton *m_updateCancelBtn;
    int m_updateProgress;
    int m_selectedModule;
    bool m_appUpdateRunning = false;
    QTimer *m_updateTimer;
    
    // 固件升级相关
    QLabel *m_firmwareIntroLabel;
    QLabel *m_firmwareStateLabel;
    QLabel *m_firmwareVersionLabel;
    QLabel *m_firmwareProgressText;
    QProgressBar *m_firmwareProgressBar;
    QWidget *m_firmwareProgressRowWidget;
    QPushButton *m_firmwareStartBtn;
    QPushButton *m_firmwareCancelBtn;
    QTabWidget *m_updateTabWidget;
    // MCU串口升级
    QLabel *m_mcuStateLabel;
    QLabel *m_mcuProgressText;
    QProgressBar *m_mcuProgressBar;
    QWidget *m_mcuProgressRowWidget;
    QPushButton *m_mcuStartBtn;
    QPushButton *m_mcuCancelBtn;
    QByteArray m_mcuFirmwareData;
    QByteArray m_mcuRxBuf;
    QString m_mcuFirmwarePath;
    QTimer *m_mcuTimeoutTimer = nullptr;
    int m_mcuFileSize;
    int m_mcuBytesSent;
    int m_mcuBlockNum;
    int m_mcuNakRetries = 0;
    int m_mcuRestartRetries = 0; // 收到 'C' 整段重传次数
    int m_mcuState; // 0=idle,1=waitOTA_OK,2=waitC,3=waitC_after_name,31=waitACK,4=sentEOT,5=done
    void mcuUpgradeCleanup(const QString &statusText);
    void mcuSendFileNameBlock();
    bool mcuSendDataBlock(int blockNum);
    QString m_checkedUpdateFile;
    QString m_checkedNewVersion;
    
    OTAManager *m_otaManager;
};

#endif // SYSTEMSETTINGWINDOW_H
