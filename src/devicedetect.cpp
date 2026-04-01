#include "devicedetect.h"
#include <QApplication>
#include <QScreen>
#include <QSysInfo>
#include <QDebug>

DeviceDetect& DeviceDetect::instance()
{
    static DeviceDetect s_instance;
    return s_instance;
}

DeviceDetect::DeviceDetect()
    : m_deviceType(DEVICE_TYPE_PC),
      m_isTouchDevice(false),
      m_screenWidth(1920),
      m_screenHeight(1080)
{
    detectDevice();
}

DeviceDetect::~DeviceDetect()
{
}

void DeviceDetect::detectDevice()
{
    m_platform = QSysInfo::productType();
    m_architecture = QSysInfo::currentCpuArchitecture();

    qDebug() << "Platform:" << m_platform;
    qDebug() << "Architecture:" << m_architecture;

#ifdef CAR_DESK_DEVICE_CARUNIT
    m_deviceType = DEVICE_TYPE_CARUNIT;
    m_isTouchDevice = true;
    m_screenWidth = 1280;
    m_screenHeight = 720;
    qDebug() << "Device Type: Car Unit (T507)";
#else
    m_deviceType = DEVICE_TYPE_PC;
    m_isTouchDevice = false;

    if (QApplication::primaryScreen()) {
        QScreen *screen = QApplication::primaryScreen();
        QRect rect = screen->geometry();
        m_screenWidth = rect.width();
        m_screenHeight = rect.height();
    }

    qDebug() << "Device Type: PC";
#endif

    qDebug() << "Screen Resolution:" << m_screenWidth << "x" << m_screenHeight;
    qDebug() << "Touch Device:" << m_isTouchDevice;
}

DeviceDetect::DeviceType DeviceDetect::getDeviceType() const
{
    return m_deviceType;
}

QString DeviceDetect::getDeviceTypeString() const
{
    return (m_deviceType == DEVICE_TYPE_CARUNIT) ? "carunit" : "pc";
}

QString DeviceDetect::getPlatform() const
{
    return m_platform;
}

QString DeviceDetect::getArchitecture() const
{
    return m_architecture;
}

bool DeviceDetect::isTouchDevice() const
{
    return m_isTouchDevice;
}

int DeviceDetect::getScreenWidth() const
{
    return m_screenWidth;
}

int DeviceDetect::getScreenHeight() const
{
    return m_screenHeight;
}
