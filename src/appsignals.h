#ifndef APPSIGNALS_H
#define APPSIGNALS_H

#include <QObject>
#include <QApplication>

/**
 * AppSignals — 应用级信号总线（单例）
 */
class AppSignals : public QObject {
    Q_OBJECT
public:
    static AppSignals *instance() {
        static AppSignals inst;
        return &inst;
    }

    /** 返回当前时钟格式字符串（"HH:mm" 或 "hh:mm AP"）*/
    static QString timeFormat() {
        const QVariant v = qApp ? qApp->property("appClock24h") : QVariant();
        return (v.isValid() && v.toBool()) ? QStringLiteral("HH:mm") : QStringLiteral("hh:mm AP");
    }

    /** Change volume up/down by one app level. */
    static void changeVolume(int delta);
    /** Set the app volume level directly (0..10). */
    static void setVolumeLevel(int level);
    /** Toggle mute on/off based on the current app volume level. */
    static void toggleMute();

signals:
    /** 音量等级变化（0–10 整数等级，对应 amixer 0-100% 归一化）*/
    void volumeLevelChanged(int level);

    /** 时钟制式变化：true=24小时，false=12小时 */
    void clockFormatChanged(bool use24h);

    /** 蓝牙连接状态变化：true=已连接，false=已断开 */
    void bluetoothConnectedChanged(bool connected);

    /** USB 设备插拔状态变化：true=已插入，false=已拔出 */
    void usbStateChanged(bool connected);

    /** TF 卡插拔状态变化：true=已插入且已挂载，false=未检测到有效存储 */
    void sdcardStateChanged(bool connected);

    /** TF 录像目录内容变化（新增/删除分段等） */
    void recordFilesChanged();

    /** 车速变化（km/h，来自 CAN TCO1 报文） */
    void vehicleSpeedChanged(float speedKmh);

    /** 大灯/仪表照明状态：true=开（夜晚），false=关（白天） */
    void illuminationChanged(bool on);

private:
    AppSignals() = default;
};

#endif // APPSIGNALS_H
