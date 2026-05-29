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

    // 行车模式偏好：270=后路布局 271=左右布局（AhdLayoutSpec::mode）
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
