#include "processguard.h"

#include <QDir>
#include <QFile>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

int ProcessGuard::s_lockFd = -1;

namespace {

constexpr char kLockPath[] = "/tmp/cardesk.lock";

bool cmdlineIsCarDesk(const QByteArray &cmdline)
{
    return cmdline.contains("CarDesk");
}

} // namespace

bool ProcessGuard::hasOtherCarDeskInstances(QString *detailOut)
{
    const qint64 selfPid = static_cast<qint64>(::getpid());
    QStringList others;
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0 || pid == selfPid) {
            continue;
        }
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(entry));
        if (!cmdline.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (!cmdlineIsCarDesk(cmdline.readAll())) {
            continue;
        }
        others << entry;
    }
    if (others.isEmpty()) {
        return false;
    }
    if (detailOut) {
        *detailOut = QStringLiteral(
                          "[CarDesk] Other instance(s) running (pid %1); camera may be in use. "
                          "D-state processes cannot be killed with killall; reboot, then retry.")
                          .arg(others.join(QLatin1String(", ")));
    }
    return true;
}

bool ProcessGuard::tryAcquireInstanceLock(QString *errorOut)
{
    QString zombieDetail;
    if (hasOtherCarDeskInstances(&zombieDetail)) {
        if (errorOut) {
            *errorOut = zombieDetail;
        }
        return false;
    }

    s_lockFd = ::open(kLockPath, O_CREAT | O_RDWR, 0644);
    if (s_lockFd < 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("[CarDesk] Cannot create instance lock %1: %2")
                            .arg(QLatin1String(kLockPath), QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (::fcntl(s_lockFd, F_SETLK, &lock) != 0) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                "[CarDesk] Another instance is running (lock %1 held). Reboot if the process is stuck.")
                            .arg(QLatin1String(kLockPath));
        }
        ::close(s_lockFd);
        s_lockFd = -1;
        return false;
    }

    if (::ftruncate(s_lockFd, 0) == 0) {
        const QByteArray pidLine = QByteArray::number(::getpid()) + '\n';
        (void)::write(s_lockFd, pidLine.constData(), static_cast<unsigned>(pidLine.size()));
    }
    return true;
}

void ProcessGuard::releaseInstanceLock()
{
    if (s_lockFd < 0) {
        return;
    }
    struct flock lock {};
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    (void)::fcntl(s_lockFd, F_SETLK, &lock);
    ::close(s_lockFd);
    s_lockFd = -1;
}
