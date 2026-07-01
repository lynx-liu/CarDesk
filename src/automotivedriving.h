#ifndef AUTOMOTIVEDRIVING_H
#define AUTOMOTIVEDRIVING_H

// 行车影像内（窗口已打开时）按 TCO1 车速切换布局：
//   ≥35km/h → 行车模式（左右二分屏/倒车，见设置）；<25km/h → 普通模式(360)；25~35 滞回
// 不因车速自动弹出行车影像；转向/倒车 CAN 可自动弹出
// 无转向/倒车（含≥3s 未收到 OEL/LC）持续 3s 后才退出 CAN 布局
void automotiveNotifyUserOpenedDrivingImage();
void automotiveNotifyUserClosedDrivingImage();
void automotiveUpdateVehicleSpeed(float speedKmh);

void automotiveSyncCanSignals(int rTurn, int lTurn, int backup);

// 信号未变时仅刷新 CAN 活动时间戳，避免每帧重复走布局/预览逻辑
void automotiveRefreshCanBusActivity();

void automotiveSetBackupSignal(bool on);
void automotiveSetLeftTurnSignal(bool on);
void automotiveSetRightTurnSignal(bool on);
void automotiveSetIllumination(bool on);

bool automotiveCanUserCloseDrivingImage();

int automotiveLayoutForUserOpen();

// 系统设置「行车视频」开关变更：仅影响车速≥35km/h 时是否切入行车布局
void automotiveOnDrivingVideoSettingChanged();

#endif // AUTOMOTIVEDRIVING_H
