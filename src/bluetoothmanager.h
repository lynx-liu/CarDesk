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
    bool sendDtmfDigit(const QString &digit);
    bool setCallMute(bool mute);
    void answerCall();
    void rejectCall();
    void hangupCall();
    void requestPhonebookDownload();
    void requestCallLogDownload();

    bool playPauseMusic();
    bool stopMusic();
    bool nextTrack();
    bool previousTrack();
    bool setPlaybackMode(int mode);
    bool connectLastA2dpDevice();
    bool disconnectA2dp();
    bool queryA2dpTrackInfo();

signals:
    void deviceFound(const QString &name, const QString &address);
    void pairedDeviceFound(const QString &name, const QString &address);
    void deviceConnected(const QString &name);
    void deviceDisconnected();
    void callStatusChanged(int status);
    void callNumberUpdated(const QString &number, const QString &source);
    void phonebookEntryReceived(const QString &name, const QString &number);
    void callLogEntryReceived(int type, const QString &name, const QString &number, const QString &timeText = QString());
    void phonebookDownloadFinished();
    void callLogDownloadFinished();
    void scanStarted();
    void scanFinished();
    void pairedQueryFinished();
    void pairedDeviceCleared(const QString &address);
    void bluetoothEnabledChanged(bool enabled);
    void bluetoothNameChanged(const QString &name);
    void bluetoothPinChanged(const QString &pin);
    void a2dpTrackInfoChanged(const QString &title, const QString &artist, const QString &album, const QString &time, int index, int total);
    void a2dpPlaybackStateChanged(bool playing);
    void a2dpProgressChanged(qint64 positionMs, qint64 totalMs);
    void error(const QString &errorMsg);

private slots:
    void onSerialReadyRead();

private:
    bool ensureInitialized();
    bool openSerialPort();
    bool sendAtCommand(const QString &command);
    void parseLine(const QByteArray &line);
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
    bool m_a2dpPlaying = false;
    qint64 m_a2dpDurationMs = 0;
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
