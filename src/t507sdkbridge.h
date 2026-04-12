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

    // ── TM2313 DSP ────────────────────────────────────────────────────────────
    // 设置声场模式，写入 /dev/tm2313 ioctl 接口。
    // 支持的模式名：立体声 环绕声 低音增强 高音增强 平衡音 音场定位 降噪音效 虚拟声场 响度补偿
    // 非 CARUNIT 编译时静默忽略（PC 调试无操作）。
    static void setSoundMode(const QString &modeName);
};

#endif // T507SDKBRIDGE_H
