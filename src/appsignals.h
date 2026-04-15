#ifndef APPSIGNALS_H
#define APPSIGNALS_H

#include <QObject>
#include <QApplication>
#include <QStringList>

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

    /**
     * Run `amixer` with given args and refresh system volume level afterwards.
     * This will emit `volumeLevelChanged(int)` after reading current volume.
     */
    static void runAmixer(const QStringList &args, QObject *parent = nullptr);

signals:
    /** 音量等级变化（0–10 整数等级，对应 amixer 0-100% 归一化）*/
    void volumeLevelChanged(int level);

    /** 时钟制式变化：true=24小时，false=12小时 */
    void clockFormatChanged(bool use24h);

private:
    AppSignals() = default;
};

#endif // APPSIGNALS_H
