#include "usbmanager.h"
#include "t507sdkbridge.h"
#include <QDebug>
#include <QStorageInfo>

USBManager::USBManager(QObject *parent)
    : QObject(parent)
{
}

USBManager::~USBManager() {
}

void USBManager::scanDevices() {
    qDebug() << "Scanning for USB devices...";
    
    m_connectedDevices.clear();
    
    const QStringList preferredMounts = {
        T507SdkBridge::usbMountPath(),
        T507SdkBridge::tfMountPath(),
        T507SdkBridge::emmcUdiskMountPath()
    };

    // 获取所有挂载的存储设备
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isReadOnly() && storage.isValid()) {
            QString deviceName = storage.displayName();
            QString devicePath = storage.rootPath();

            if (!preferredMounts.contains(devicePath) && !devicePath.startsWith("/media") && !devicePath.startsWith("/mnt")) {
                continue;
            }
            
            qDebug() << "Found device:" << deviceName << "at" << devicePath;
            
            m_connectedDevices.append(devicePath);
            emit deviceDetected(deviceName, devicePath);
        }
    }
    
    emit scanFinished();
}

QStringList USBManager::getUSBDevices() const {
    return m_connectedDevices;
}

bool USBManager::mountDevice(const QString &devicePath) {
    qDebug() << "Mounting device:" << devicePath;
    
    // TODO: 优先对接 T507 SDK 板级挂载流程（/mnt/usb/sda1, /mnt/sdcard/mmcblk1p1, /mnt/UDISK）
    
    return true;
}

bool USBManager::unmountDevice(const QString &devicePath) {
    qDebug() << "Unmounting device:" << devicePath;
    
    // TODO: 优先对接 T507 SDK 板级卸载流程
    
    return true;
}

QStringList USBManager::getFilesFromDevice(const QString &devicePath, const QString &fileType) {
    QStringList files;
    
    qDebug() << "Getting files from device:" << devicePath;
    
    // TODO: 实现文件列表获取逻辑
    // 根据 fileType 筛选（如 "audio", "video" 等）
    
    return files;
}
