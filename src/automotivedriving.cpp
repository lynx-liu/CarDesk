#include "automotivedriving.h"

#include "ahdsettings.h"
#include "drivingimagewindow.h"
#include "mainwindow.h"
#include "mediamanager.h"

#include <QApplication>
#include <QDebug>
#include <QWidget>

namespace {

static float s_vehicleSpeedKmh = 0.f;
// 仅在行车影像界面内生效：≥35 行车布局，<25 四分屏，25~35 滞回
static bool s_speedDrivingMode = false;
static bool s_backupOn = false;
static bool s_leftTurnOn = false;
static bool s_rightTurnOn = false;
static bool s_illuminationOn = false;
static int s_activeSignalMode = 0;
static int s_lastTurnSignalMode = 0;
static bool s_userOpenedDrivingImage = false;

MainWindow *findMainWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *main = qobject_cast<MainWindow *>(widget)) {
            return main;
        }
    }
    return nullptr;
}

DrivingImageWindow *findDrivingImageWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *drive = qobject_cast<DrivingImageWindow *>(widget)) {
            return drive;
        }
    }
    return nullptr;
}

bool isDrivingImageWindowVisible()
{
    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        return drive->isVisible();
    }
    return false;
}

void activateDrivingImageMode(int mode)
{
    MainWindow *main = findMainWindow();
    if (main && main->mediaManager()) {
        main->mediaManager()->pausePlaybackForOcclusion();
    }

    DrivingImageWindow *drive = findDrivingImageWindow();
    if (!drive && main) {
        main->showDrivingImageForAutomotive(mode);
        return;
    }

    if (drive) {
        drive->applyAutomotiveMode(mode);
        if (!drive->isVisible()) {
            drive->show();
        }
        drive->raise();
        drive->activateWindow();
    }
}

bool hasOtherVisibleTopLevel(const QWidget *except)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget == except || widget->objectName() == QLatin1String("transitionOverlay")) {
            continue;
        }
        if (widget->isVisible()) {
            return true;
        }
    }
    return false;
}

void hideDrivingImageWindow()
{
    DrivingImageWindow *drive = findDrivingImageWindow();
    if (drive && drive->isVisible()) {
        drive->hide();
    }

    MainWindow *main = findMainWindow();
    if (main && main->mediaManager()) {
        main->mediaManager()->resumePlaybackAfterInterruption();
    }

    if (main && !hasOtherVisibleTopLevel(drive)) {
        main->show();
        main->raise();
        main->activateWindow();
    }
}

int layoutModeForSpeedDriving()
{
    return (AhdSettings::instance().preferredDrivingMode() == 271) ? 180 : 3;
}

void refreshActiveSignalMode()
{
    if (s_backupOn) {
        s_activeSignalMode = 270;
        return;
    }
    if (s_leftTurnOn && s_rightTurnOn) {
        s_activeSignalMode = (s_lastTurnSignalMode == 271 || s_lastTurnSignalMode == 272)
                                 ? s_lastTurnSignalMode
                                 : 272;
        return;
    }
    if (s_rightTurnOn) {
        s_activeSignalMode = 272;
        return;
    }
    if (s_leftTurnOn) {
        s_activeSignalMode = 271;
        return;
    }
    s_activeSignalMode = 0;
    s_lastTurnSignalMode = 0;
}

void updateSpeedDrivingMode()
{
    const bool wasDriving = s_speedDrivingMode;
    if (s_vehicleSpeedKmh >= 35.f) {
        s_speedDrivingMode = true;
    } else if (s_vehicleSpeedKmh < 25.f) {
        s_speedDrivingMode = false;
    }

    if (wasDriving != s_speedDrivingMode) {
        qDebug() << "[Automotive] speed driving mode:" << s_speedDrivingMode
                 << "speed=" << s_vehicleSpeedKmh;
    }
}

int resolveAutomotiveLayoutMode()
{
    if (s_activeSignalMode != 0) {
        return s_activeSignalMode;
    }
    if (s_speedDrivingMode) {
        return layoutModeForSpeedDriving();
    }
    return 360;
}

// 车速仅切换已打开的行车影像布局，不自动弹出窗口
void applySpeedLayoutIfDrivingImageVisible()
{
    if (!isDrivingImageWindowVisible()) {
        return;
    }

    const int mode = resolveAutomotiveLayoutMode();
    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        qDebug() << "[Automotive] speed layout" << mode << "speed=" << s_vehicleSpeedKmh
                 << "drivingMode=" << s_speedDrivingMode;
        drive->applyAutomotiveMode(mode);
    }
}

// 转向/倒车 CAN：可自动弹出影像；用户手动进入也保持
void applyCanAutomotiveDisplayState()
{
    const bool needWindow = s_activeSignalMode != 0 || s_userOpenedDrivingImage;
    if (!needWindow) {
        hideDrivingImageWindow();
        return;
    }

    const int mode = resolveAutomotiveLayoutMode();
    qDebug() << "[Automotive] CAN layout" << mode << "backup=" << s_backupOn
             << "lTurn=" << s_leftTurnOn << "rTurn=" << s_rightTurnOn;
    activateDrivingImageMode(mode);
}

void updateSignalAndApply()
{
    refreshActiveSignalMode();
    applyCanAutomotiveDisplayState();
    applySpeedLayoutIfDrivingImageVisible();
}

bool canUserCloseDrivingImage()
{
    return s_activeSignalMode == 0;
}

int layoutForUserOpen()
{
    return resolveAutomotiveLayoutMode();
}

void notifyUserOpenedDrivingImage()
{
    s_userOpenedDrivingImage = true;
}

void notifyUserClosedDrivingImage()
{
    if (!canUserCloseDrivingImage()) {
        qDebug() << "[Automotive] ignore manual exit: turn/reverse active";
        return;
    }

    s_userOpenedDrivingImage = false;
    applyCanAutomotiveDisplayState();
}

void updateVehicleSpeed(float speedKmh)
{
    s_vehicleSpeedKmh = speedKmh;
    updateSpeedDrivingMode();
    applySpeedLayoutIfDrivingImageVisible();
}

void syncCanSignals(int rTurn, int lTurn, int backup)
{
    const bool newLeft = lTurn != 0;
    const bool newRight = rTurn != 0;
    if (newLeft && !s_leftTurnOn) {
        s_lastTurnSignalMode = 271;
    }
    if (newRight && !s_rightTurnOn) {
        s_lastTurnSignalMode = 272;
    }
    s_backupOn = backup != 0;
    s_leftTurnOn = newLeft;
    s_rightTurnOn = newRight;
    updateSignalAndApply();
}

void setBackupSignal(bool on)
{
    s_backupOn = on;
    updateSignalAndApply();
}

void setLeftTurnSignal(bool on)
{
    if (on) {
        s_rightTurnOn = false;
        s_lastTurnSignalMode = 271;
    } else if (s_lastTurnSignalMode == 271) {
        s_lastTurnSignalMode = s_rightTurnOn ? 272 : 0;
    }
    s_leftTurnOn = on;
    updateSignalAndApply();
}

void setRightTurnSignal(bool on)
{
    if (on) {
        s_leftTurnOn = false;
        s_lastTurnSignalMode = 272;
    } else if (s_lastTurnSignalMode == 272) {
        s_lastTurnSignalMode = s_leftTurnOn ? 271 : 0;
    }
    s_rightTurnOn = on;
    updateSignalAndApply();
}

void setIllumination(bool on)
{
    if (s_illuminationOn == on) {
        return;
    }
    s_illuminationOn = on;
    qDebug() << "[Automotive] cabin illumination" << (on ? "ON" : "OFF");
}

} // namespace

void automotiveNotifyUserOpenedDrivingImage()
{
    notifyUserOpenedDrivingImage();
}

void automotiveNotifyUserClosedDrivingImage()
{
    notifyUserClosedDrivingImage();
}

bool automotiveCanUserCloseDrivingImage()
{
    return canUserCloseDrivingImage();
}

int automotiveLayoutForUserOpen()
{
    return layoutForUserOpen();
}

void automotiveUpdateVehicleSpeed(float speedKmh)
{
    updateVehicleSpeed(speedKmh);
}

void automotiveSyncCanSignals(int rTurn, int lTurn, int backup)
{
    syncCanSignals(rTurn, lTurn, backup);
}

void automotiveSetBackupSignal(bool on)
{
    setBackupSignal(on);
}

void automotiveSetLeftTurnSignal(bool on)
{
    setLeftTurnSignal(on);
}

void automotiveSetRightTurnSignal(bool on)
{
    setRightTurnSignal(on);
}

void automotiveSetIllumination(bool on)
{
    setIllumination(on);
}
