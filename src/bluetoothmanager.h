#ifndef BLUETOOTHMANAGER_H
#define BLUETOOTHMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSerialPort>

class BluetoothManager : public QObject {
    Q_OBJECT

public:
    explicit BluetoothManager(QObject *parent = nullptr);
    ~BluetoothManager() override;

    void scanDevices();
    void stopScan();
    void queryPairedDevices();
    void clearPairedDevice(const QString &address);
    void connectDevice(const QString &deviceAddress = QString());
    void disconnectDevice();
    bool isConnected() const;
    bool isBluetoothEnabled() const;
    QString getConnectedDeviceName() const;
    QString getConnectedDeviceAddress() const;
    QString getBluetoothName() const;
    QString getBluetoothPin() const;

    void setBluetoothOn(bool enabled);
    void setDeviceName(const QString &name);
    void setPin(const QString &pin);
    void queryBluetoothSettings();
    void queryConnectedDevice();

    void dialNumber(const QString &number);
    void answerCall();
    void rejectCall();
    void hangupCall();

    void playPauseMusic();
    void stopMusic();
    void nextTrack();
    void previousTrack();
    void connectLastA2dpDevice();
    void disconnectA2dp();

signals:
    void deviceFound(const QString &name, const QString &address);
    void pairedDeviceFound(const QString &name, const QString &address);
    void deviceConnected(const QString &name);
    void deviceDisconnected();
    void scanStarted();
    void scanFinished();
    void pairedQueryFinished();
    void pairedDeviceCleared(const QString &address);
    void bluetoothEnabledChanged(bool enabled);
    void bluetoothNameChanged(const QString &name);
    void bluetoothPinChanged(const QString &pin);
    void error(const QString &errorMsg);

private slots:
    void onSerialReadyRead();

private:
    bool ensureInitialized();
    bool openSerialPort();
    void sendAtCommand(const QString &command);
    void parseLine(const QString &line);
    QString normalizeAddress(const QString &addr) const;

    enum class BluetoothQueryState {
        None,
        QueryStatus,
        QueryName,
        QueryPin
    };

    bool m_isConnected;
    QString m_connectedDeviceName;
    QString m_connectedDeviceAddress;
    QString m_deviceName;
    QString m_pinCode;
    BluetoothQueryState m_queryState;

    QSerialPort *m_port;
    QByteArray m_buffer;
    bool m_initialized;
    bool m_scanning;
    bool m_btEnabled;
    bool m_queryingPaired;
    QString m_clearingAddress;
};

#endif // BLUETOOTHMANAGER_H
