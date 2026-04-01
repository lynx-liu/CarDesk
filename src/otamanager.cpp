#include "otamanager.h"
#include "progressmonitor.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>

OTAManager::OTAManager(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_isUpdating(false)
    , m_monitorThread(nullptr)
    , m_progressMonitor(nullptr)
    , m_progressTimeout(nullptr)
    , m_lastProgress(0)
{
}

OTAManager::~OTAManager()
{
    stopProgressMonitor();
    if (m_process) {
        if (m_process->state() == QProcess::Running) {
            m_process->kill();
        }
        delete m_process;
    }
}

bool OTAManager::checkUpdateFile(QString &outFilePath)
{
    QString usbPath = findUSBDevicePath();
    if (usbPath.isEmpty()) {
        qWarning() << "No USB device found!";
        return false;
    }

    QString swuFile = findSWUFile(usbPath);
    if (swuFile.isEmpty()) {
        qWarning() << "No .swu file found in USB device!";
        return false;
    }

    outFilePath = swuFile;
    qDebug() << "Found update file:" << swuFile;
    return true;
}

bool OTAManager::startUpdate(const QString &swuFilePath, const QString &newVersion)
{
    if (m_isUpdating) {
        qWarning() << "Update already in progress!";
        return false;
    }

    if (!QFile::exists(swuFilePath)) {
        qWarning() << "File not found:" << swuFilePath;
        emit updateFailed(QStringLiteral("升级文件未找到"));
        return false;
    }

    m_isUpdating = true;
    m_lastProgress = 0;
    emit updateStarted();
    emit updateStateChanged(QStringLiteral("准备升级..."));

    // 启动进度监控（在单独的线程中）
    startProgressMonitor();

    // 创建并启动进程执行ota.sh脚本
    if (m_process) {
        delete m_process;
    }
    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &OTAManager::onProcessReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &OTAManager::onProcessReadyReadStandardError);
    connect(m_process, QOverload<int>::of(&QProcess::finished),
            this, &OTAManager::onProcessFinished);

    // 构建ota.sh脚本的命令行
    QStringList arguments;
    arguments << swuFilePath;
    if (!newVersion.isEmpty()) {
        arguments << newVersion;
    }

    // 统一使用系统内置升级脚本
    const QString scriptPath = "/etc/ota.sh";
    if (!QFile::exists(scriptPath)) {
        qWarning() << "OTA script not found:" << scriptPath;
        emit updateFailed(QStringLiteral("系统升级脚本不存在: /etc/ota.sh"));
        m_isUpdating = false;
        stopProgressMonitor();
        return false;
    }

    qDebug() << "Starting OTA update with:" << scriptPath << arguments;
    emit updateStateChanged(QStringLiteral("正在升级系统..."));

    m_process->start("sh", QStringList() << scriptPath << arguments);
    if (!m_process->waitForStarted()) {
        qWarning() << "Failed to start OTA process!";
        emit updateFailed(QStringLiteral("无法启动升级进程"));
        m_isUpdating = false;
        stopProgressMonitor();
        return false;
    }

    return true;
}

void OTAManager::cancelUpdate()
{
    if (m_process && m_process->state() == QProcess::Running) {
        qDebug() << "Cancelling OTA update...";
        m_process->kill();
        emit updateCancelled();
    }
    stopProgressMonitor();
}

QString OTAManager::getCurrentVersion()
{
    QProcess process;
    process.start("fw_printenv", QStringList() << "-n" << "swu_version");
    process.waitForFinished();

    QString version = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    qDebug() << "Current version:" << version;
    return version;
}

void OTAManager::rebootSystem()
{
    qDebug() << "System will reboot in 5 seconds...";
    QTimer::singleShot(5000, this, [this]() {
        QProcess::startDetached("reboot");
    });
}

void OTAManager::onProcessReadyReadStandardOutput()
{
    if (!m_process) return;

    QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    qDebug() << "OTA stdout:" << output;

    // 解析输出中的进度信息
    if (output.contains("Upgrade success", Qt::CaseInsensitive)) {
        emit updateProgress(100);
        emit updateStateChanged(QStringLiteral("系统升级完成！"));
    } else if (output.contains("ERROR", Qt::CaseSensitive)) {
        emit updateStateChanged(output);
    }

    // 发送状态更新信号
    emit updateStateChanged(output.trimmed());
}

void OTAManager::onProcessReadyReadStandardError()
{
    if (!m_process) return;

    QString error = QString::fromLocal8Bit(m_process->readAllStandardError());
    qDebug() << "OTA stderr:" << error;
    emit updateStateChanged(error.trimmed());
}

void OTAManager::onProcessFinished(int exitCode)
{
    m_isUpdating = false;
    stopProgressMonitor();

    if (exitCode == 0) {
        qDebug() << "OTA update completed successfully!";
        emit updateProgress(100);
        emit updateStateChanged(QStringLiteral("升级成功，准备重启系统..."));
        emit updateCompleted();

        // 延迟后重启系统
        QTimer::singleShot(3000, this, &OTAManager::rebootSystem);
    } else {
        QString errorMsg = QStringLiteral("升级失败（代码:%1）").arg(exitCode);
        qWarning() << "OTA update failed!" << errorMsg;
        emit updateStateChanged(errorMsg);
        emit updateFailed(errorMsg);
    }
}

QString OTAManager::findUSBDevicePath()
{
    // 尝试查找常见的USB挂载点
    QStringList possiblePaths = {
        "/mnt/usb",
        "/mnt/usb0",
        "/media/usb",
        "/media/usb0",
        "/run/media",
        "/mnt"
    };

    for (const QString &path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            // 检查目录中是否有子目录或.swu文件
            dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
            QFileInfoList entries = dir.entryInfoList();

            if (!entries.isEmpty()) {
                qDebug() << "Found device at:" << path;
                return path;
            }
        }
    }

    // 尝试从/sys/block查找USB设备
    QDir sysBlock("/sys/block");
    if (sysBlock.exists()) {
        QFileInfoList drives = sysBlock.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &drive : drives) {
            QString driveName = drive.fileName();
            if (driveName.startsWith("sd")) {  // USB通常是sd*
                // 查找挂载点
                QProcess process;
                process.start("grep", QStringList() << "-l" << driveName << "/proc/mounts");
                process.waitForFinished();

                QString mounts = QString::fromLocal8Bit(process.readAllStandardOutput());
                if (!mounts.isEmpty()) {
                    qDebug() << "Found USB device:" << driveName;
                    return "/mnt/" + driveName;  // 假设挂载在/mnt
                }
            }
        }
    }

    return "";
}

QString OTAManager::findSWUFile(const QString &usbPath)
{
    QDir dir(usbPath);
    if (!dir.exists()) {
        return "";
    }

    // 递归搜索.swu文件
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &entry : entries) {
        if (entry.isFile() && entry.suffix().toLower() == "swu") {
            qDebug() << "Found .swu file:" << entry.filePath();
            return entry.filePath();
        } else if (entry.isDir()) {
            QString result = findSWUFile(entry.filePath());
            if (!result.isEmpty()) {
                return result;
            }
        }
    }

    return "";
}

QString OTAManager::parseVersionFromSWU(const QString &swuFilePath)
{
    // 使用strings命令提取.swu文件中的版本信息
    QProcess process;
    process.start("strings", QStringList() << swuFilePath);
    process.waitForFinished();

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    QStringList lines = output.split('\n');

    for (const QString &line : lines) {
        if (line.contains("version", Qt::CaseInsensitive)) {
            qDebug() << "Parsed version:" << line;
            return line;
        }
    }

    return "";
}

QString OTAManager::getCurrentPartition()
{
    QProcess process;
    process.start("fw_printenv", QStringList() << "-n" << "boot_partition");
    process.waitForFinished();

    QString partition = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    qDebug() << "Current partition:" << partition;
    return partition;
}

void OTAManager::startProgressMonitor()
{
    if (m_progressMonitor) {
        return;  // 已经在运行
    }

    // 创建独立线程运行进度监控
    m_monitorThread = new QThread(this);
    m_progressMonitor = new ProgressMonitor();
    m_progressMonitor->moveToThread(m_monitorThread);

    // 连接信号
    connect(m_progressMonitor, &ProgressMonitor::connected, this, &OTAManager::onProgressMonitorConnected);
    connect(m_progressMonitor, &ProgressMonitor::disconnected, this, &OTAManager::onProgressMonitorDisconnected);
    connect(m_progressMonitor, &ProgressMonitor::progress, this, &OTAManager::onProgressMonitorProgress);
    connect(m_progressMonitor, &ProgressMonitor::errorOccurred, this, &OTAManager::onProgressMonitorError);
    
    // 线程启动和停止
    connect(m_monitorThread, &QThread::started, m_progressMonitor, &ProgressMonitor::start);
    connect(this, &OTAManager::destroyed, m_progressMonitor, &ProgressMonitor::stop);

    // 启动线程
    m_monitorThread->start();

    // 添加超时保护，如果5秒内没有连接成功，认为swupdate可能不支持socket
    m_progressTimeout = new QTimer(this);
    m_progressTimeout->setInterval(5000);
    connect(m_progressTimeout, &QTimer::timeout, this, &OTAManager::onProgressMonitorTimeout);
    m_progressTimeout->start();
}

void OTAManager::stopProgressMonitor()
{
    if (m_progressTimeout) {
        m_progressTimeout->stop();
    }

    if (m_progressMonitor) {
        m_progressMonitor->stop();
    }

    if (m_monitorThread) {
        m_monitorThread->quit();
        m_monitorThread->wait(2000);
        m_monitorThread->deleteLater();
        m_monitorThread = nullptr;
    }

    if (m_progressMonitor) {
        m_progressMonitor->deleteLater();
        m_progressMonitor = nullptr;
    }
}

void OTAManager::onProgressMonitorConnected()
{
    qDebug() << "OTA: Progress monitor connected to swupdate socket";
    if (m_progressTimeout) {
        m_progressTimeout->stop();
    }
}

void OTAManager::onProgressMonitorDisconnected()
{
    qDebug() << "OTA: Progress monitor disconnected from swupdate socket";
}

void OTAManager::onProgressMonitorProgress(int percentage, const QString &state)
{
    // 只有当进度确实改变时才发出信号，避免过度更新UI
    if (percentage != m_lastProgress) {
        m_lastProgress = percentage;
        emit updateProgress(percentage);
    }
    emit updateStateChanged(state);
}

void OTAManager::onProgressMonitorError(const QString &error)
{
    qWarning() << "OTA: Progress monitor error:" << error;
    // 不中止升级，继续等待升级完成
}

void OTAManager::onProgressMonitorTimeout()
{
    // 如果5秒后没有连接到socket，可能是swupdate版本问题
    // 或socket路径不同，不需要中止升级，继续进行
    qWarning() << "OTA: Progress monitor socket connect timeout, continuing without real-time progress";
    if (m_progressTimeout) {
        m_progressTimeout->stop();
    }
}
