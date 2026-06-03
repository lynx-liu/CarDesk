#ifndef AUTOMOTIVEDRIVING_H
#define AUTOMOTIVEDRIVING_H

// 行车影像内（窗口已打开时）按 TCO1 车速切换布局：
//   ≥35km/h → 行车模式（左右二分屏/倒车，见设置）；<25km/h → 普通模式(360)；25~35 滞回
// 不因车速自动弹出行车影像；转向/倒车 CAN 仍可自动弹出并优先布局
void automotiveNotifyUserOpenedDrivingImage();
void automotiveNotifyUserClosedDrivingImage();
void automotiveUpdateVehicleSpeed(float speedKmh);

void automotiveSyncCanSignals(int rTurn, int lTurn, int backup);

void automotiveSetBackupSignal(bool on);
void automotiveSetLeftTurnSignal(bool on);
void automotiveSetRightTurnSignal(bool on);
void automotiveSetIllumination(bool on);

bool automotiveCanUserCloseDrivingImage();

int automotiveLayoutForUserOpen();

#endif // AUTOMOTIVEDRIVING_H
