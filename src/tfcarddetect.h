#ifndef TFCARDDETECT_H
#define TFCARDDETECT_H

#include <QObject>
#include <QString>
#include <QStringList>

// TF/SD 块设备识别（逻辑参考 /data/sdk-work/USBtest UsbMonitor::scanTfCard）
struct TfCardInfo {
    QString blockDev;
    QString sysPath;
    QString type;
    qint64 sizeBytes = 0;
    int partitionCount = 0;
    bool isPresent = false;
};

class TfCardDetect {
public:
    static TfCardInfo scan();
    static void cleanupStaleMountPoints(const QString &baseDir = QStringLiteral("/mnt/sdcard"));
};

// 全应用唯一的 TF/录像存储检测：硬件 + /mnt/sdcard 挂载卷，结果写入 AhdRecordStore 缓存并广播 AppSignals
class TfCardMonitor : public QObject {
    Q_OBJECT

public:
    static TfCardMonitor *instance();

    void start();
    void stop();
    void rescanNow();

    bool isRecordStorageReady() const { return m_recordReady; }
    QStringList recordRoots() const { return m_recordRoots; }

signals:
    void recordStorageChanged(bool ready);

private slots:
    void onUeventReady();
    void onInotifyReady();
    void onDelayedRescan();
    void onMountWatchTriggered(const QString &path);

private:
    explicit TfCardMonitor(QObject *parent = nullptr);
    ~TfCardMonitor() override;

    bool initNetlink();
    void closeNetlink();
    bool initInotify();
    void closeInotify();
    void rescanStorageState();
    bool scanRecordVolumes(QStringList *volumeRootsOut);

    int m_nlSock = -1;
    class QSocketNotifier *m_nlNotifier = nullptr;
    int m_inotifyFd = -1;
    class QSocketNotifier *m_inotifyNotifier = nullptr;
    class QFileSystemWatcher *m_mountWatcher = nullptr;
    class QTimer *m_mountDebounce = nullptr;
    class QTimer *m_fallbackTimer = nullptr;

    bool m_recordReady = false;
    QStringList m_recordRoots;
    QString m_blockDev;
};

#endif // TFCARDDETECT_H
