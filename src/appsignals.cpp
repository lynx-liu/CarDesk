#include "appsignals.h"
#include "t507sdkbridge.h"
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>

namespace {
static int loadSavedVolumeLevel()
{
    QSettings settings;
    return qBound(0, settings.value("sound/volumeLevel", 10).toInt(), 10);
}
}

static void saveVolumeLevel(int level)
{
    QSettings settings;
    settings.setValue("sound/volumeLevel", qBound(0, level, 10));
}

static void publishVolumeLevel(int level)
{
    const int bounded = qBound(0, level, 10);
    QCoreApplication::instance()->setProperty("appVolumeLevel", bounded);
    AppSignals::instance()->volumeLevelChanged(bounded);
}

void AppSignals::setVolumeLevel(int level)
{
    const int boundedLevel = qBound(0, level, 10);
    qDebug() << "[AppSignals] setVolumeLevel:" << boundedLevel;
#ifdef CAR_DESK_DEVICE_CARUNIT
    T507SdkBridge::setVolumeLevel(boundedLevel);
    publishVolumeLevel(boundedLevel);
#else
    publishVolumeLevel(boundedLevel);
#endif
    saveVolumeLevel(boundedLevel);
}

void AppSignals::toggleMute()
{
    const int currentLevel = qBound(0, qApp->property("appVolumeLevel").toInt(), 10);
    if (currentLevel == 0) {
        const QVariant previous = qApp->property("appVolumePrevLevel");
        const int restoreLevel = previous.isValid() ? qBound(1, previous.toInt(), 10) : 10;
        qDebug() << "[AppSignals] toggleMute: unmute restoreLevel=" << restoreLevel;
        setVolumeLevel(restoreLevel);
    } else {
        qApp->setProperty("appVolumePrevLevel", currentLevel);
        qDebug() << "[AppSignals] toggleMute: mute currentLevel=" << currentLevel;
        setVolumeLevel(0);
    }
}

void AppSignals::changeVolume(int delta)
{
    const QVariant currentVolume = qApp->property("appVolumeLevel");
    const int currentLevel = currentVolume.isValid()
        ? qBound(0, currentVolume.toInt(), 10)
        : loadSavedVolumeLevel();
    const int targetLevel = qBound(0, currentLevel + delta, 10);
    AppSignals::setVolumeLevel(targetLevel);
}


