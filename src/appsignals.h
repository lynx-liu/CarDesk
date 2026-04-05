#ifndef APPSIGNALS_H
#define APPSIGNALS_H

#include <QObject>

/**
 * AppSignals — 应用级信号总线（单例）
 *
 * 用于在 GlobalKeyFilter（main.cpp）与所有子界面顶部栏之间传递
 * 硬件音量键引起的音量变化，使各界面的音量图标和数值实时同步。
 */
class AppSignals : public QObject {
    Q_OBJECT
public:
    static AppSignals *instance() {
        static AppSignals inst;
        return &inst;
    }

signals:
    /** 音量等级变化（0–10 整数等级，对应 amixer 0-100% 归一化） */
    void volumeLevelChanged(int level);

private:
    AppSignals() = default;
};

#endif // APPSIGNALS_H
