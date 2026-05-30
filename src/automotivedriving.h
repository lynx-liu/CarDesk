#ifndef AUTOMOTIVEDRIVING_H
#define AUTOMOTIVEDRIVING_H

// 规范行车模式：车速≥35km/h 进入、<25km/h 退出；转向/倒车 CAN 信号优先。
void automotiveNotifyUserOpenedDrivingImage();
void automotiveNotifyUserClosedDrivingImage();
void automotiveUpdateVehicleSpeed(float speedKmh);

// CAN 灯光状态（LC/OEL 合并后一次性同步）
void automotiveSyncCanSignals(int rTurn, int lTurn, int backup);

// 车机功能键：各信号独立开/关
void automotiveSetBackupSignal(bool on);
void automotiveSetLeftTurnSignal(bool on);
void automotiveSetRightTurnSignal(bool on);
void automotiveSetIllumination(bool on);

// 转向/倒车进行中不允许用户关闭影像；仅 360 与车速行车模式可关
bool automotiveCanUserCloseDrivingImage();

// 用户进入「行车影像」时应使用的布局（含车速行车模式读设置）
int automotiveLayoutForUserOpen();

#endif // AUTOMOTIVEDRIVING_H
