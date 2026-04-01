#ifndef BLUETOOTHMANAGER_H
#define BLUETOOTHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class BluetoothManager : public QObject {
    Q_OBJECT

public:
    explicit BluetoothManager(QObject *parent = nullptr);
    ~BluetoothManager();
    
    void scanDevices();
    void connectDevice(const QString &deviceAddress);
    void disconnectDevice();
    bool isConnected() const;
    QString getConnectedDeviceName() const;

signals:
    void deviceFound(const QString &name, const QString &address);
    void deviceConnected(const QString &name);
    void deviceDisconnected();
    void scanFinished();
    void error(const QString &errorMsg);

private:
    bool m_isConnected;
    QString m_connectedDeviceName;
    QString m_connectedDeviceAddress;
};

#endif // BLUETOOTHMANAGER_H
