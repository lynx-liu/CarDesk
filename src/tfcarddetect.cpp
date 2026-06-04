#include "tfcarddetect.h"
#include "ahdrecordstore.h"
#include "appsignals.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/netlink.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

QString readSysFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromLatin1(f.readLine()).trimmed();
}

QStringList listSysfsDir(const QString &path)
{
    QStringList result;
    DIR *dp = opendir(path.toUtf8().constData());
    if (!dp) {
        return result;
    }
    struct dirent *de = nullptr;
    while ((de = readdir(dp)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        result.append(QString::fromUtf8(de->d_name));
    }
    closedir(dp);
    return result;
}

QString parseUeventField(const QByteArray &data, const char *field)
{
    const QByteArray prefix = QByteArray(field) + "=";
    const QList<QByteArray> lines = data.split('\0');
    for (const QByteArray &line : lines) {
        if (line.startsWith(prefix)) {
            return QString::fromLatin1(line.mid(prefix.size()));
        }
    }
    return QString();
}

// 内置 eMMC 一般为 mmcblk0，可移动 TF 为 mmcblk1+
bool isRemovableMmcCandidate(const QString &blk)
{
    static const QRegularExpression re(QStringLiteral("^mmcblk(\\d+)$"));
    const QRegularExpressionMatch match = re.match(blk);
    if (!match.hasMatch()) {
        return false;
    }
    return match.captured(1).toInt() >= 1;
}

const char *kSdcardMountParent = "/mnt/sdcard";

bool shouldSkipDirName(const QString &name)
{
    return name.isEmpty() || name.startsWith(QLatin1Char('.'));
}

QString decodeMountPath(const QString &raw)
{
    QString path = raw;
    path.replace(QStringLiteral("\\040"), QStringLiteral(" "));
    return path;
}

QString normalizeDevicePath(const QString &dev)
{
    QString path = decodeMountPath(dev.trimmed());
    if (path.startsWith(QLatin1String("/dev/"))) {
        return path;
    }
    const int blockIdx = path.indexOf(QStringLiteral("/block/"));
    if (blockIdx >= 0) {
        const QString blk = path.mid(blockIdx + 7);
        if (blk.startsWith(QLatin1String("mmcblk"))) {
            return QStringLiteral("/dev/%1").arg(blk);
        }
    }
    if (path.startsWith(QLatin1String("mmcblk"))) {
        return QStringLiteral("/dev/%1").arg(path);
    }
    return path;
}

bool isMmcBlockDevicePresent(const QString &devPath)
{
    const QString path = normalizeDevicePath(devPath);
    if (!path.contains(QLatin1String("mmcblk"))) {
        return false;
    }
    if (!QFileInfo::exists(path)) {
        return false;
    }
    static const QRegularExpression partRe(QStringLiteral("^/dev/(mmcblk\\d+)(?:p\\d+)?$"));
    const QRegularExpressionMatch match = partRe.match(path);
    if (!match.hasMatch()) {
        return true;
    }
    const QString disk = match.captured(1);
    if (disk == QStringLiteral("mmcblk0")) {
        return false;
    }
    return QDir(QStringLiteral("/sys/block/%1").arg(disk)).exists();
}

QString mountDeviceForPath(const QString &absPath)
{
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString bestDev;
    int bestLen = -1;
    while (!mounts.atEnd()) {
        const QByteArray raw = mounts.readLine();
        const int sp1 = raw.indexOf(' ');
        if (sp1 < 0) {
            continue;
        }
        const int sp2 = raw.indexOf(' ', sp1 + 1);
        const QString dev = decodeMountPath(QString::fromLatin1(raw.left(sp1)));
        const QString mnt = decodeMountPath(
            (sp2 > sp1) ? QString::fromLatin1(raw.mid(sp1 + 1, sp2 - sp1 - 1))
                        : QString::fromLatin1(raw.mid(sp1 + 1)).trimmed());
        if (mnt != absPath && !absPath.startsWith(mnt + QLatin1Char('/'))) {
            continue;
        }
        if (mnt.size() > bestLen) {
            bestLen = mnt.size();
            bestDev = dev;
        }
    }
    mounts.close();
    return bestDev;
}

bool isLiveMmcMount(const QString &absPath)
{
    const QString dev = mountDeviceForPath(absPath);
    return !dev.isEmpty() && isMmcBlockDevicePresent(dev);
}

bool isSdcardVolumeReady(const QString &absPath, bool *staleEmptyOut = nullptr)
{
    const QFileInfo info(absPath);
    if (!info.exists() || !info.isDir()) {
        return false;
    }
    const QStringList entries =
        QDir(absPath).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    if (!entries.isEmpty()) {
        return true;
    }
    if (isLiveMmcMount(absPath)) {
        return true;
    }
    if (staleEmptyOut) {
        *staleEmptyOut = true;
    }
    return false;
}

void appendUniqueRoot(QStringList *roots, const QString &path)
{
    if (!roots || path.isEmpty() || roots->contains(path)) {
        return;
    }
    roots->append(path);
}

} // namespace

TfCardInfo TfCardDetect::scan()
{
    TfCardInfo info;

    const QStringList blocks = listSysfsDir(QStringLiteral("/sys/block"));
    const QStringList mmcBlks =
        blocks.filter(QRegularExpression(QStringLiteral("^mmcblk\\d+$")));

    for (const QString &blk : mmcBlks) {
        if (!isRemovableMmcCandidate(blk)) {
            continue;
        }

        const QString sysPath = QStringLiteral("/sys/block/%1").arg(blk);
        QString type = readSysFile(sysPath + QStringLiteral("/device/type"));

        bool isSd = false;
        if (!type.isEmpty()) {
            if (type == QStringLiteral("SD")) {
                isSd = true;
            } else {
                continue;
            }
        } else {
            const QString manfid = readSysFile(sysPath + QStringLiteral("/device/manfid"));
            const QString oemid = readSysFile(sysPath + QStringLiteral("/device/oemid"));
            if (!manfid.isEmpty() || !oemid.isEmpty()) {
                isSd = true;
            } else {
                continue;
            }
        }

        const QString sizeStr = readSysFile(sysPath + QStringLiteral("/size"));
        bool ok = false;
        const qint64 sectors = sizeStr.toLongLong(&ok);
        const qint64 sizeBytes = ok ? sectors * 512LL : 0;

        const QByteArray devPath = QStringLiteral("/dev/%1").arg(blk).toLatin1();
        const int fd = open(devPath.constData(), O_RDONLY | O_NONBLOCK);
        bool canOpen = (fd >= 0);
        bool canRead = false;
        if (fd >= 0) {
            qint64 blkSize = 0;
            if (ioctl(fd, BLKGETSIZE64, &blkSize) == 0 && blkSize > 0) {
                canRead = true;
            }
            close(fd);
        }

        if (!canOpen || !canRead || !ok || sectors <= 0) {
            continue;
        }

        const QString manfid = readSysFile(sysPath + QStringLiteral("/device/manfid"));
        if (manfid.isEmpty()) {
            continue;
        }

        const QStringList entries = listSysfsDir(sysPath);
        const QStringList parts =
            entries.filter(QRegularExpression(QStringLiteral("^%1\\d+$").arg(blk)));

        info.blockDev = blk;
        info.sysPath = sysPath;
        info.type = type.isEmpty() ? QStringLiteral("SD") : type;
        info.sizeBytes = sizeBytes;
        info.partitionCount = parts.size();
        info.isPresent = true;
        break;
    }

    return info;
}

void TfCardDetect::cleanupStaleMountPoints(const QString &baseDir)
{
    if (baseDir.isEmpty()) {
        return;
    }

    const TfCardInfo tf = scan();
    QDir mountDir(baseDir);
    if (!mountDir.exists()) {
        return;
    }

    const QStringList entries = mountDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry.startsWith(QLatin1Char('.'))) {
            continue;
        }
        const QString mountPoint = baseDir + QLatin1Char('/') + entry;

        bool deviceExists = false;
        if (tf.isPresent) {
            if (entry == tf.blockDev || entry.startsWith(tf.blockDev)) {
                deviceExists = true;
            }
        }

        const QString blockPath = QStringLiteral("/sys/block/%1").arg(entry);
        if (!deviceExists && !QDir(blockPath).exists()) {
            QDir dir(mountPoint);
            const QStringList contents = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
            if (contents.isEmpty()) {
                qDebug() << "[TfCard] remove stale mount dir:" << mountPoint;
                dir.rmdir(QStringLiteral("."));
            }
        }
    }
}

TfCardMonitor *TfCardMonitor::instance()
{
    static TfCardMonitor monitor;
    return &monitor;
}

TfCardMonitor::TfCardMonitor(QObject *parent)
    : QObject(parent)
{
}

TfCardMonitor::~TfCardMonitor()
{
    stop();
}

void TfCardMonitor::rescanNow()
{
    rescanStorageState();
}

bool TfCardMonitor::scanRecordVolumes(QStringList *volumeRootsOut)
{
    TfCardDetect::cleanupStaleMountPoints(QString::fromUtf8(kSdcardMountParent));
    const TfCardInfo hw = TfCardDetect::scan();
    if (!hw.isPresent) {
        if (volumeRootsOut) {
            volumeRootsOut->clear();
        }
        return false;
    }

    QStringList roots;
    bool found = false;

    QDir sdcardRoot(QString::fromUtf8(kSdcardMountParent));
    if (sdcardRoot.exists()) {
        const QFileInfoList entries =
            sdcardRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : entries) {
            const QString name = fi.fileName();
            if (shouldSkipDirName(name)) {
                continue;
            }
            const QString path = fi.absoluteFilePath();
            bool staleEmpty = false;
            if (isSdcardVolumeReady(path, &staleEmpty)) {
                found = true;
                appendUniqueRoot(&roots, path);
            } else if (staleEmpty) {
                QDir(path).rmdir(QStringLiteral("."));
            }
        }
    }

    if (volumeRootsOut) {
        *volumeRootsOut = roots;
    }
    return found;
}

bool TfCardMonitor::initNetlink()
{
    m_nlSock = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (m_nlSock < 0) {
        qWarning() << "[TfCard] netlink socket failed:" << strerror(errno);
        return false;
    }

    int bufsize = 64 * 1024;
    setsockopt(m_nlSock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 1;
    addr.nl_pid = static_cast<unsigned int>(getpid());

    if (bind(m_nlSock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        qWarning() << "[TfCard] netlink bind failed:" << strerror(errno);
        close(m_nlSock);
        m_nlSock = -1;
        return false;
    }

    m_nlNotifier = new QSocketNotifier(m_nlSock, QSocketNotifier::Read, this);
    connect(m_nlNotifier, &QSocketNotifier::activated, this, &TfCardMonitor::onUeventReady);
    return true;
}

void TfCardMonitor::closeNetlink()
{
    if (m_nlNotifier) {
        m_nlNotifier->setEnabled(false);
        delete m_nlNotifier;
        m_nlNotifier = nullptr;
    }
    if (m_nlSock >= 0) {
        close(m_nlSock);
        m_nlSock = -1;
    }
}

bool TfCardMonitor::initInotify()
{
    m_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotifyFd < 0) {
        qWarning() << "[TfCard] inotify init failed:" << strerror(errno);
        return false;
    }

    const int wd = inotify_add_watch(m_inotifyFd, "/dev", IN_CREATE | IN_DELETE);
    if (wd < 0) {
        qWarning() << "[TfCard] inotify watch /dev failed:" << strerror(errno);
        close(m_inotifyFd);
        m_inotifyFd = -1;
        return false;
    }

    m_inotifyNotifier = new QSocketNotifier(m_inotifyFd, QSocketNotifier::Read, this);
    connect(m_inotifyNotifier, &QSocketNotifier::activated, this, &TfCardMonitor::onInotifyReady);
    return true;
}

void TfCardMonitor::closeInotify()
{
    if (m_inotifyNotifier) {
        m_inotifyNotifier->setEnabled(false);
        delete m_inotifyNotifier;
        m_inotifyNotifier = nullptr;
    }
    if (m_inotifyFd >= 0) {
        close(m_inotifyFd);
        m_inotifyFd = -1;
    }
}

void TfCardMonitor::rescanStorageState()
{
    const TfCardInfo hw = TfCardDetect::scan();
    QStringList roots;
    const bool ready = scanRecordVolumes(&roots);

    if (ready == m_recordReady && roots == m_recordRoots
        && (!ready || hw.blockDev == m_blockDev)) {
        return;
    }

    m_recordReady = ready;
    m_recordRoots = roots;
    m_blockDev = hw.blockDev;

    AhdRecordStore::updateStorageCache(ready, roots);
    qDebug() << "[TfCard] record storage" << (ready ? "ready" : "absent") << roots;
    emit recordStorageChanged(ready);
    emit AppSignals::instance()->sdcardStateChanged(ready);
}

void TfCardMonitor::onUeventReady()
{
    char buf[4096];
    const ssize_t len = recv(m_nlSock, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    if (len <= 0) {
        return;
    }
    buf[len] = '\0';

    const QByteArray data(buf, static_cast<int>(len));
    const QString action = parseUeventField(data, "ACTION");
    const QString devPath = parseUeventField(data, "DEVPATH");
    const QString subsystem = parseUeventField(data, "SUBSYSTEM");
    if (devPath.isEmpty()) {
        return;
    }

    const QString devName = devPath.split(QLatin1Char('/')).last();
    if (subsystem == QStringLiteral("mmc")
        || (subsystem == QStringLiteral("block") && devName.contains(QStringLiteral("mmcblk")))) {
        Q_UNUSED(action);
        QTimer::singleShot(1000, this, &TfCardMonitor::onDelayedRescan);
    }
}

void TfCardMonitor::onInotifyReady()
{
    char buf[4096];
    const ssize_t len = read(m_inotifyFd, buf, sizeof(buf));
    if (len <= 0) {
        return;
    }

    bool hasMmcblk = false;
    ssize_t offset = 0;
    while (offset < len) {
        const auto *event = reinterpret_cast<const struct inotify_event *>(buf + offset);
        const QString name(event->name);
        if (name.contains(QStringLiteral("mmcblk"))) {
            hasMmcblk = true;
            break;
        }
        offset += sizeof(struct inotify_event) + event->len;
    }

    if (hasMmcblk) {
        QTimer::singleShot(1000, this, &TfCardMonitor::onDelayedRescan);
    }
}

void TfCardMonitor::onDelayedRescan()
{
    rescanStorageState();
}

void TfCardMonitor::onMountWatchTriggered(const QString &path)
{
    Q_UNUSED(path);
    if (m_mountDebounce) {
        m_mountDebounce->start();
    }
}

void TfCardMonitor::start()
{
    if (m_fallbackTimer) {
        return;
    }

    rescanStorageState();
    initNetlink();
    initInotify();

    m_mountWatcher = new QFileSystemWatcher(this);
    m_mountWatcher->addPath(QStringLiteral("/proc/mounts"));
    if (QDir(QString::fromUtf8(kSdcardMountParent)).exists()) {
        m_mountWatcher->addPath(QString::fromUtf8(kSdcardMountParent));
    }
    connect(m_mountWatcher, &QFileSystemWatcher::fileChanged, this,
            &TfCardMonitor::onMountWatchTriggered);
    connect(m_mountWatcher, &QFileSystemWatcher::directoryChanged, this,
            &TfCardMonitor::onMountWatchTriggered);

    m_mountDebounce = new QTimer(this);
    m_mountDebounce->setSingleShot(true);
    m_mountDebounce->setInterval(1500);
    connect(m_mountDebounce, &QTimer::timeout, this, &TfCardMonitor::onDelayedRescan);

    m_fallbackTimer = new QTimer(this);
    m_fallbackTimer->setInterval(30000);
    connect(m_fallbackTimer, &QTimer::timeout, this, &TfCardMonitor::onDelayedRescan);
    m_fallbackTimer->start();
}

void TfCardMonitor::stop()
{
    closeNetlink();
    closeInotify();
    if (m_fallbackTimer) {
        m_fallbackTimer->stop();
        m_fallbackTimer->deleteLater();
        m_fallbackTimer = nullptr;
    }
    if (m_mountDebounce) {
        m_mountDebounce->deleteLater();
        m_mountDebounce = nullptr;
    }
    if (m_mountWatcher) {
        m_mountWatcher->deleteLater();
        m_mountWatcher = nullptr;
    }
}
