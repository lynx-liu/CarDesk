#include "ahdsettings.h"

#include <QSettings>

AhdSettings &AhdSettings::instance()
{
    static AhdSettings inst;
    return inst;
}

AhdSettings::AhdSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

bool AhdSettings::recordingEnabled() const
{
    return m_recordingEnabled;
}

void AhdSettings::setRecordingEnabled(bool enabled)
{
    if (m_recordingEnabled == enabled) {
        return;
    }
    m_recordingEnabled = enabled;
    save();
    emit recordingEnabledChanged(enabled);
}

int AhdSettings::preferredDrivingMode() const
{
    return m_preferredDrivingMode;
}

void AhdSettings::setPreferredDrivingMode(int mode)
{
    if (mode == 3) {
        mode = 270;
    } else if (mode == 4 || mode == 5) {
        mode = 271;
    }
    if (mode != 270 && mode != 271) {
        mode = 270;
    }
    if (m_preferredDrivingMode == mode) {
        return;
    }
    m_preferredDrivingMode = mode;
    save();
    emit preferredDrivingModeChanged(mode);
}

void AhdSettings::load()
{
    QSettings s(QStringLiteral("CarDesk"), QStringLiteral("DrivingImage"));
    m_recordingEnabled = s.value(QStringLiteral("recordingEnabled"), true).toBool();
    int mode = s.value(QStringLiteral("preferredDrivingMode"), 270).toInt();
    if (mode == 3) {
        mode = 270;
    } else if (mode == 4 || mode == 5) {
        mode = 271;
    } else if (mode != 270 && mode != 271) {
        mode = 270;
    }
    m_preferredDrivingMode = mode;
}

void AhdSettings::save()
{
    QSettings s(QStringLiteral("CarDesk"), QStringLiteral("DrivingImage"));
    s.setValue(QStringLiteral("recordingEnabled"), m_recordingEnabled);
    s.setValue(QStringLiteral("preferredDrivingMode"), m_preferredDrivingMode);
}
