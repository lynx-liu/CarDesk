#ifndef USBMANAGER_H
#define USBMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

struct USBDevice {
    QString name;
    QString path;
    QString vendor;
    QString model;
    qint64 totalSize;
    qint64 availableSize;
};

class USBManager : public QObject {
    Q_OBJECT

public:
    explicit USBManager(QObject *parent = nullptr);
    ~USBManager();
    
    void scanDevices();
    QStringList getUSBDevices() const;
    bool mountDevice(const QString &devicePath);
    bool unmountDevice(const QString &devicePath);
    QStringList getFilesFromDevice(const QString &devicePath, const QString &fileType = "");

signals:
    void deviceDetected(const QString &name, const QString &path);
    void deviceRemoved(const QString &path);
    void scanFinished();
    void error(const QString &errorMsg);

private:
    QStringList m_connectedDevices;
};

#endif // USBMANAGER_H
