#include "automotivedriving.h"

#include "ahdsettings.h"
#include "drivingimagewindow.h"
#include "mainwindow.h"
#include "mediamanager.h"

#include <QApplication>
#include <QDebug>

namespace {

static float s_vehicleSpeedKmh = 0.f;
static bool s_speedDrivingActive = false;
static bool s_backupOn = false;
static bool s_leftTurnOn = false;
static bool s_rightTurnOn = false;
static bool s_illuminationOn = false;
static int s_activeSignalMode = 0;
static bool s_userOpenedDrivingImage = false;
static bool s_userDismissedDrivingImage = false;

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

void activateDrivingImageMode(int mode)
{
    MainWindow *main = findMainWindow();
    if (main && main->mediaManager()) {
        main->mediaManager()->pausePlaybackForOcclusion();
    }

    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        drive->setDrivingMode(mode);
        if (!drive->isVisible()) {
            drive->show();
        }
        drive->raise();
        drive->activateWindow();
        return;
    }

    if (main) {
        main->showDrivingImageForAutomotive(mode);
    }
}

void hideDrivingImageWindow()
{
    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        if (!drive->isVisible()) {
            return;
        }
        drive->hide();
    }

    MainWindow *main = findMainWindow();
    if (main) {
        main->show();
        main->raise();
        main->activateWindow();
        if (main->mediaManager()) {
            main->mediaManager()->resumePlaybackAfterInterruption();
        }
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
    } else if (s_rightTurnOn) {
        s_activeSignalMode = 272;
    } else if (s_leftTurnOn) {
        s_activeSignalMode = 271;
    } else {
        s_activeSignalMode = 0;
    }
}

int resolveAutomotiveLayoutMode()
{
    if (s_activeSignalMode != 0) {
        return s_activeSignalMode;
    }
    if (s_speedDrivingActive) {
        return layoutModeForSpeedDriving();
    }
    return 360;
}

bool shouldShowDrivingImage()
{
    if (s_activeSignalMode != 0) {
        return true;
    }
    if (s_speedDrivingActive && !s_userDismissedDrivingImage) {
        return true;
    }
    if (s_userOpenedDrivingImage) {
        return true;
    }
    return false;
}

void applyAutomotiveDisplayState()
{
    if (!shouldShowDrivingImage()) {
        hideDrivingImageWindow();
        return;
    }

    const int mode = resolveAutomotiveLayoutMode();
    qDebug() << "[Automotive] apply layout mode" << mode << "speed=" << s_vehicleSpeedKmh
             << "speedDriving=" << s_speedDrivingActive << "backup=" << s_backupOn
             << "lTurn=" << s_leftTurnOn << "rTurn=" << s_rightTurnOn;
    activateDrivingImageMode(mode);
}

void updateSpeedDrivingActive()
{
    const bool wasActive = s_speedDrivingActive;
    if (s_vehicleSpeedKmh >= 35.f) {
        s_speedDrivingActive = true;
    } else if (s_vehicleSpeedKmh < 25.f) {
        s_speedDrivingActive = false;
        s_userDismissedDrivingImage = false;
    }

    if (wasActive != s_speedDrivingActive) {
        qDebug() << "[Automotive] speed driving active:" << s_speedDrivingActive
                 << "speed=" << s_vehicleSpeedKmh;
    }
}

void updateSignalAndApply()
{
    refreshActiveSignalMode();
    applyAutomotiveDisplayState();
}

bool canUserCloseDrivingImage()
{
    return s_activeSignalMode == 0;
}

int layoutForUserOpen()
{
    if (s_activeSignalMode != 0) {
        return s_activeSignalMode;
    }
    if (s_speedDrivingActive) {
        return layoutModeForSpeedDriving();
    }
    return 360;
}

void notifyUserOpenedDrivingImage()
{
    s_userOpenedDrivingImage = true;
    s_userDismissedDrivingImage = false;
}

void notifyUserClosedDrivingImage()
{
    if (!canUserCloseDrivingImage()) {
        qDebug() << "[Automotive] ignore close: turn/reverse active";
        return;
    }

    s_userOpenedDrivingImage = false;
    s_userDismissedDrivingImage = true;
    applyAutomotiveDisplayState();
}

void updateVehicleSpeed(float speedKmh)
{
    s_vehicleSpeedKmh = speedKmh;
    updateSpeedDrivingActive();
    applyAutomotiveDisplayState();
}

void syncCanSignals(int rTurn, int lTurn, int backup)
{
    s_backupOn = backup != 0;
    s_leftTurnOn = lTurn != 0;
    s_rightTurnOn = rTurn != 0;
    updateSignalAndApply();
}

void setBackupSignal(bool on)
{
    s_backupOn = on;
    updateSignalAndApply();
}

void setLeftTurnSignal(bool on)
{
    s_leftTurnOn = on;
    updateSignalAndApply();
}

void setRightTurnSignal(bool on)
{
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
