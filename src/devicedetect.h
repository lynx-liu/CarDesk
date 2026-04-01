#ifndef DEVICEDETECT_H
#define DEVICEDETECT_H

#include <QString>

class DeviceDetect {
public:
    enum DeviceType {
        DEVICE_TYPE_PC,
        DEVICE_TYPE_CARUNIT
    };

    static DeviceDetect& instance();
    
    DeviceType getDeviceType() const;
    QString getDeviceTypeString() const;
    QString getPlatform() const;
    QString getArchitecture() const;
    bool isTouchDevice() const;
    
    int getScreenWidth() const;
    int getScreenHeight() const;

private:
    DeviceDetect();
    ~DeviceDetect();
    
    void detectDevice();
    
    DeviceType m_deviceType;
    QString m_platform;
    QString m_architecture;
    bool m_isTouchDevice;
    int m_screenWidth;
    int m_screenHeight;
};

#endif // DEVICEDETECT_H
