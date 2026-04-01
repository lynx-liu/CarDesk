#include "t507sdkbridge.h"

QStringList T507SdkBridge::ahdVideoNodes()
{
    // Based on T507 board debug document: single-channel AHD test uses /dev/video2-5.
    return {QStringLiteral("/dev/video2"), QStringLiteral("/dev/video3"), QStringLiteral("/dev/video4"), QStringLiteral("/dev/video5")};
}

QString T507SdkBridge::ahdChannelName(int index)
{
    static const QStringList channels = {
        QStringLiteral("AHD1（通道1）"),
        QStringLiteral("AHD2（通道2）"),
        QStringLiteral("AHD3（通道3）"),
        QStringLiteral("AHD4（通道4）")
    };
    return channels[index % channels.size()];
}

QString T507SdkBridge::ahdTestCommand(int index)
{
    const int sdkNode = (index % 4) + 2;
    return QStringLiteral("sdktest 1 %1").arg(sdkNode);
}

QString T507SdkBridge::usbMountPath()
{
    return QStringLiteral("/mnt/usb/sda1");
}

QString T507SdkBridge::tfMountPath()
{
    return QStringLiteral("/mnt/sdcard/mmcblk1p1");
}

QString T507SdkBridge::emmcUdiskMountPath()
{
    return QStringLiteral("/mnt/UDISK");
}

QStringList T507SdkBridge::bluetoothInitCommands()
{
    return {
        QStringLiteral("insmod /lib/modules/4.9.170/xr829.ko"),
        QStringLiteral("insmod /lib/modules/4.9.170/xradio_btlpm.ko"),
        QStringLiteral("hciattach -s 115200 /dev/ttyS1 xradio")
    };
}
