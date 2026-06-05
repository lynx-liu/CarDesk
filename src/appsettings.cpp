#include "appsettings.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

const char kDrivingVideoEnabledKey[] = "display/drivingVideoEnabled";

} // namespace

namespace AppSettings {

bool drivingVideoEnabled()
{
    return QSettings().value(QLatin1String(kDrivingVideoEnabledKey), true).toBool();
}

void setDrivingVideoEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(QLatin1String(kDrivingVideoEnabledKey), enabled);
    settings.sync();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->setProperty("appDrivingVideoEnabled", enabled);
    }
}

void syncAppPropertiesFromSettings()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->setProperty("appDrivingVideoEnabled",
                                                 drivingVideoEnabled());
    }
}

bool debugMode()
{
    if (!QCoreApplication::instance()) {
        return false;
    }
    return QCoreApplication::instance()->property("appDebugMode").toBool();
}

void setDebugMode(bool enabled)
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->setProperty("appDebugMode", enabled);
    }
}

} // namespace AppSettings
