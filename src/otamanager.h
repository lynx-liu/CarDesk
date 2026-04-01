#ifndef OTAMANAGER_H
#define OTAMANAGER_H

#include <QObject>
#include <QString>
#include <QThread>

class QProcess;
class QLocalSocket;
class QTimer;
class ProgressMonitor;

class OTAManager : public QObject {
    Q_OBJECT

public:
    explicit OTAManager(QObject *parent = nullptr);
    ~OTAManager();

    // 检查USB设备中是否有升级文件
    bool checkUpdateFile(QString &outFilePath);

    // 启动升级过程
    bool startUpdate(const QString &swuFilePath, const QString &newVersion = "");

    // 取消升级
    void cancelUpdate();

    // 获取当前系统版本
    QString getCurrentVersion();

    // 获取当前启动分区（bootA/bootB）
    QString getCurrentPartition();

    // 解析升级包中的版本信息
    QString parseVersionFromSWU(const QString &swuFilePath);

    // 重启系统
    void rebootSystem();

signals:
    void updateProgress(int percentage);
    void updateStateChanged(const QString &state);
    void updateStarted();
    void updateCompleted();
    void updateFailed(const QString &error);
    void updateCancelled();

private slots:
    void onProcessReadyReadStandardOutput();
    void onProcessReadyReadStandardError();
    void onProcessFinished(int exitCode);
    void onProgressMonitorConnected();
    void onProgressMonitorDisconnected();
    void onProgressMonitorProgress(int percentage, const QString &state);
    void onProgressMonitorError(const QString &error);
    void onProgressMonitorTimeout();

private:
    // 查找USB设备路径
    QString findUSBDevicePath();

    // 在USB设备中查找.swu文件
    QString findSWUFile(const QString &usbPath);

    // 启动进度监控
    void startProgressMonitor();

    // 停止进度监控
    void stopProgressMonitor();

    QProcess *m_process;
    bool m_isUpdating;
    
    // 进度监控相关
    QThread *m_monitorThread;
    ProgressMonitor *m_progressMonitor;
    QTimer *m_progressTimeout;
    int m_lastProgress;
};

#endif // OTAMANAGER_H
