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

class SystemSettingWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SystemSettingWindow(QWidget *parent = nullptr);

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;

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

private:
    QString findAppUpdateArchive(QString *usbRoot = nullptr) const;
    bool applyAppUpdateFromArchive(const QString &archivePath, QString *errorMessage);
    void setupUI();
    QWidget *createDisplayPage();
    QWidget *createSoundPage();
    QWidget *createBluetoothPage();
    QWidget *createInfoPage();
    QWidget *createFactoryPage();
    QWidget *createUpdatePage();

    QStackedWidget *m_pages;
    QListWidget *m_subnavList;

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
    QString m_checkedUpdateFile;
    QString m_checkedNewVersion;
    
    OTAManager *m_otaManager;
};

#endif // SYSTEMSETTINGWINDOW_H
