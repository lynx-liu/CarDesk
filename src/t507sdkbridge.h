#ifndef T507SDKBRIDGE_H
#define T507SDKBRIDGE_H

#include <QString>
#include <QStringList>

class T507SdkBridge {
public:
    static QStringList ahdVideoNodes();
    static QString ahdChannelName(int index);
    static QString ahdTestCommand(int index);

    static QString usbMountPath();
    static QString tfMountPath();
    static QString emmcUdiskMountPath();

    static QStringList bluetoothInitCommands();
};

#endif // T507SDKBRIDGE_H
