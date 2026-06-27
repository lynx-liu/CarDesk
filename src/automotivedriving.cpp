#include "automotivedriving.h"

#include "appsettings.h"
#include "ahdsettings.h"
#include "drivingimagewindow.h"
#include "mainwindow.h"
#include "mcuserialreader.h"
#include "mediamanager.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QPointer>
#include <QTimer>
#include <QWidget>

static qint64 g_lastLcFrameMs = 0;

namespace {

static float s_vehicleSpeedKmh = 0.f;
static bool s_speedDrivingMode = false;
static bool s_backupOn = false;
static bool s_leftTurnOn = false;
static bool s_rightTurnOn = false;
static bool s_illuminationOn = false;
static int s_activeSignalMode = 0;
static int s_lastTurnSignalMode = 0;
static bool s_userOpenedDrivingImage = false;
static qint64 s_noTurnReverseSinceMs = 0;
static QTimer *s_canReleaseWatchdog = nullptr;
static constexpr int kCanReleaseDelayMs = 3000;
static constexpr int kCanWatchdogIntervalMs = 200;

void applyCanAutomotiveDisplayState(bool forceReengage);
void applySpeedLayoutIfDrivingImageVisible();
void applyCanSignalTransition();
void tickCanReleaseWatchdog();
void releaseCanLayout();
void ensureCanReleaseWatchdog();

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

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

void activateDrivingImageMode(int mode, bool forceReengage)
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

    if (!drive) {
        return;
    }

    // CAN 弹出：forceReengage 或行车窗未 visible 时走完整 show；持连期间仅换布局。
    const bool drivingOnScreen = drive->isVisible();
    qDebug() << "[Automotive] present driving mainVisible=" << (main && main->isVisible())
             << "driveVisible=" << drive->isVisible() << "onScreen=" << drivingOnScreen
             << "mode=" << mode << "force=" << forceReengage;

    if (drivingOnScreen && !forceReengage) {
        drive->applyAutomotiveMode(mode, forceReengage);
        drive->raise();
        drive->activateWindow();
        return;
    }

    if (forceReengage && drive->isVisible()) {
        drive->hide();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 30);
    }

    if (main) {
        main->showDrivingImageForAutomotive(mode);
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
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }

    MainWindow *main = findMainWindow();
    if (main && main->mediaManager()) {
        main->mediaManager()->resumePlaybackAfterInterruption();
    }

    if (main && !hasOtherVisibleTopLevel(drive)) {
        if (!main->isVisible()) {
            main->show();
        }
        main->raise();
        main->activateWindow();
        main->update();
        main->repaint();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        QPointer<MainWindow> mainPtr = main;
        QTimer::singleShot(0, qApp, [mainPtr]() {
            for (QWidget *tw : QApplication::topLevelWidgets()) {
                if (tw->isVisible() && tw->isWindow()) {
                    tw->update();
                }
            }
            if (mainPtr && mainPtr->isVisible()) {
                mainPtr->repaint();
            }
        });
    }
}

int layoutModeForSpeedDriving()
{
    return (AhdSettings::instance().preferredDrivingMode() == 271) ? 180 : 3;
}

int computeRawCanMode()
{
    if (s_backupOn) {
        return 270;
    }
    if (s_leftTurnOn && s_rightTurnOn) {
        return (s_lastTurnSignalMode == 271 || s_lastTurnSignalMode == 272) ? s_lastTurnSignalMode
                                                                            : 272;
    }
    if (s_rightTurnOn) {
        return 272;
    }
    if (s_leftTurnOn) {
        return 271;
    }
    return 0;
}

bool isCanBusRecent()
{
    return g_lastLcFrameMs > 0 && (nowMs() - g_lastLcFrameMs) < kCanReleaseDelayMs;
}

// 有转向/倒车：近期有 OEL/LC 且解析为 ON（无数据视为无信号）
bool hasLiveTurnOrReverse()
{
    if (!isCanBusRecent()) {
        return false;
    }
    return computeRawCanMode() != 0;
}

void updateSpeedDrivingMode()
{
    const bool wasDriving = s_speedDrivingMode;
    if (AppSettings::drivingVideoEnabled() && s_vehicleSpeedKmh >= 35.f) {
        s_speedDrivingMode = true;
    } else if (!AppSettings::drivingVideoEnabled() || s_vehicleSpeedKmh < 25.f) {
        s_speedDrivingMode = false;
    }

    if (wasDriving != s_speedDrivingMode) {
        qDebug() << "[Automotive] speed driving mode:" << s_speedDrivingMode
                 << "speed=" << s_vehicleSpeedKmh
                 << "drivingVideoEnabled=" << AppSettings::drivingVideoEnabled();
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

void applySpeedLayoutIfDrivingImageVisible()
{
    DrivingImageWindow *drive = findDrivingImageWindow();
    if (!drive || !drive->isVisible()) {
        return;
    }

    const int mode = resolveAutomotiveLayoutMode();
    drive->applyAutomotiveMode(mode);
}

void applyCanAutomotiveDisplayState(bool forceReengage)
{
    const bool needWindow = s_activeSignalMode != 0 || s_userOpenedDrivingImage;
    if (!needWindow) {
        hideDrivingImageWindow();
        return;
    }

    const int mode = resolveAutomotiveLayoutMode();
    qDebug() << "[Automotive] CAN layout" << mode << "activeCan=" << s_activeSignalMode
             << "raw=" << computeRawCanMode() << "backup=" << s_backupOn
             << "lTurn=" << s_leftTurnOn << "rTurn=" << s_rightTurnOn
             << "force=" << forceReengage;
    activateDrivingImageMode(mode, forceReengage);
}

void releaseCanLayout()
{
    if (s_activeSignalMode == 0) {
        return;
    }

    const int releasedMode = s_activeSignalMode;
    s_activeSignalMode = 0;
    s_lastTurnSignalMode = 0;
    s_noTurnReverseSinceMs = 0;

    if (McuSerialReader *reader = McuSerialReader::existingShared()) {
        reader->clearCanSignalState();
    } else {
        s_backupOn = false;
        s_leftTurnOn = false;
        s_rightTurnOn = false;
        applyCanAutomotiveDisplayState(false);
    }

    qDebug() << "[Automotive] released CAN layout, was mode" << releasedMode;
    applySpeedLayoutIfDrivingImageVisible();
}

void ensureCanReleaseWatchdog()
{
    if (s_canReleaseWatchdog) {
        return;
    }
    s_canReleaseWatchdog = new QTimer(qApp);
    s_canReleaseWatchdog->setInterval(kCanWatchdogIntervalMs);
    QObject::connect(s_canReleaseWatchdog, &QTimer::timeout, qApp, []() {
        tickCanReleaseWatchdog();
    });
    s_canReleaseWatchdog->start();
}

void tickCanReleaseWatchdog()
{
    if (s_activeSignalMode == 0) {
        s_noTurnReverseSinceMs = 0;
        return;
    }

    if (hasLiveTurnOrReverse()) {
        s_noTurnReverseSinceMs = 0;
        return;
    }

    const qint64 now = nowMs();
    if (s_noTurnReverseSinceMs == 0) {
        if (g_lastLcFrameMs > 0 && now - g_lastLcFrameMs >= kCanReleaseDelayMs) {
            s_noTurnReverseSinceMs = g_lastLcFrameMs;
        } else {
            s_noTurnReverseSinceMs = now;
        }
        qDebug() << "[Automotive] no turn/reverse for 3s started, hold mode"
                 << s_activeSignalMode;
        return;
    }

    if (now - s_noTurnReverseSinceMs >= kCanReleaseDelayMs) {
        releaseCanLayout();
    }
}

void applyCanSignalTransition()
{
    const int rawMode = computeRawCanMode();

    if (rawMode != 0) {
        const bool newlyEngaged = (s_activeSignalMode == 0);
        s_noTurnReverseSinceMs = 0;
        ensureCanReleaseWatchdog();
        s_activeSignalMode = rawMode;
        applyCanAutomotiveDisplayState(newlyEngaged);
        applySpeedLayoutIfDrivingImageVisible();
        return;
    }

    if (s_activeSignalMode != 0) {
        ensureCanReleaseWatchdog();
        if (s_noTurnReverseSinceMs == 0) {
            s_noTurnReverseSinceMs = nowMs();
            qDebug() << "[Automotive] waiting 3s with no turn/reverse, hold"
                     << s_activeSignalMode;
        }
        return;
    }

    s_noTurnReverseSinceMs = 0;
    applyCanAutomotiveDisplayState(false);
}

void updateSignalAndApply()
{
    applyCanSignalTransition();
}

bool canUserCloseDrivingImage()
{
    return s_activeSignalMode == 0 && s_noTurnReverseSinceMs == 0;
}

int layoutForUserOpen()
{
    return resolveAutomotiveLayoutMode();
}

void notifyUserOpenedDrivingImage()
{
    s_userOpenedDrivingImage = true;
}

void onDrivingVideoSettingChanged()
{
    updateSpeedDrivingMode();
    applySpeedLayoutIfDrivingImageVisible();
}

void notifyUserClosedDrivingImage()
{
    if (!canUserCloseDrivingImage()) {
        qDebug() << "[Automotive] ignore manual exit: CAN hold active";
        return;
    }

    s_userOpenedDrivingImage = false;
    applyCanAutomotiveDisplayState(false);
}

void updateVehicleSpeed(float speedKmh)
{
    s_vehicleSpeedKmh = speedKmh;
    updateSpeedDrivingMode();
    tickCanReleaseWatchdog();
    applySpeedLayoutIfDrivingImageVisible();
}

void syncCanSignals(int rTurn, int lTurn, int backup)
{
    g_lastLcFrameMs = nowMs();

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
    g_lastLcFrameMs = nowMs();
    s_backupOn = on;
    updateSignalAndApply();
}

void setLeftTurnSignal(bool on)
{
    g_lastLcFrameMs = nowMs();
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
    g_lastLcFrameMs = nowMs();
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
}

} // namespace

void automotiveOnDrivingVideoSettingChanged()
{
    onDrivingVideoSettingChanged();
}

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
