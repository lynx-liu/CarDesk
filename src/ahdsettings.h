#ifndef AHDSETTINGS_H
#define AHDSETTINGS_H

#include <QObject>

// 行车影像本地设置（对齐 ui/driving_image_setting.html）
class AhdSettings : public QObject {
    Q_OBJECT

public:
    static AhdSettings &instance();

    bool recordingEnabled() const;
    void setRecordingEnabled(bool enabled);

    // 影像设置中的行车模式偏好（270=后视全屏，271=左右二分屏）：仅用于车速行车模式，
    // 在进入车速行车模式或用户打开「行车影像」且处于车速行车模式时读取。
    int preferredDrivingMode() const;
    void setPreferredDrivingMode(int mode);

signals:
    void recordingEnabledChanged(bool enabled);
    void preferredDrivingModeChanged(int mode);

private:
    explicit AhdSettings(QObject *parent = nullptr);
    void load();
    void save();

    bool m_recordingEnabled = true;
    int m_preferredDrivingMode = 270;
};

#endif // AHDSETTINGS_H
