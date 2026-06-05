#ifndef APPSETTINGS_H
#define APPSETTINGS_H

namespace AppSettings {

// 系统设置「显示模式」：行车视频开关。关闭后，行车影像普通模式下车速≥35km/h 不自动切入行车布局；默认开启
bool drivingVideoEnabled();
void setDrivingVideoEnabled(bool enabled);
void syncAppPropertiesFromSettings();

// 命令行带 debug 参数启动时为 true
bool debugMode();
void setDebugMode(bool enabled);

} // namespace AppSettings

#endif // APPSETTINGS_H
