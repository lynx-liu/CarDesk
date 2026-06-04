#include "ahdrecordstore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

const char *kSdcardMountParent = "/mnt/sdcard";
const char *kSdcardMountPrefix = "/mnt/sdcard/";

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

// 返回挂载到 absPath 的块设备路径（最长匹配），无则空
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

// 有文件 / 真实 mmc 挂载 → 有卡；仅空目录或幽灵挂载 → 无卡（可清理）
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

// /mnt/sdcard/<子目录>：非空或 mmc 块设备真实挂载；否则清理空目录（含断电拔卡后幽灵挂载）
bool isAnySdcardPresentAndCleanup(QStringList *volumeRootsOut = nullptr)
{
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

QStringList sdcardVolumeRoots()
{
    QStringList roots;
    isAnySdcardPresentAndCleanup(&roots);
    return roots;
}

QStringList existingRecordRoots()
{
    return sdcardVolumeRoots();
}

bool parseRecordTimestamp(const QString &baseName, QString *dateKey, QString *displayName)
{
    static const QRegularExpression re(
        QStringLiteral(R"((\d{4})(\d{2})(\d{2})_(\d{6}))"));
    const QRegularExpressionMatch match = re.match(baseName);
    if (!match.hasMatch()) {
        return false;
    }
    const QString dateKeyValue =
        QStringLiteral("%1.%2.%3").arg(match.captured(1), match.captured(2), match.captured(3));
    if (dateKey) {
        *dateKey = dateKeyValue;
    }
    if (displayName) {
        *displayName = QStringLiteral("%1_%2").arg(dateKeyValue, match.captured(4));
    }
    return true;
}

void collectMp4(const QString &dirPath, QStringList *out)
{
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir()) {
            if (shouldSkipDirName(fi.fileName())) {
                continue;
            }
            collectMp4(fi.absoluteFilePath(), out);
        } else if (fi.isFile() && fi.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
            out->append(fi.absoluteFilePath());
        }
    }
}

QString dateKeyFromFileInfo(const QFileInfo &fi)
{
    const QString baseName = fi.completeBaseName();
    QString dateKey;
    if (parseRecordTimestamp(baseName, &dateKey, nullptr)) {
        return dateKey;
    }

    if (baseName.size() >= 10 && baseName.at(4) == QLatin1Char('.') && baseName.at(7) == QLatin1Char('.')) {
        return baseName.left(10);
    }

    const QDir parentDir = fi.dir();
    const QString parentName = parentDir.dirName();
    if (parentName.size() >= 10 && parentName.at(4) == QLatin1Char('.') && parentName.at(7) == QLatin1Char('.')) {
        return parentName.left(10);
    }
    if (parentName.size() == 8 && parentName.at(0).isDigit()) {
        return QStringLiteral("%1.%2.%3")
            .arg(parentName.mid(0, 4))
            .arg(parentName.mid(4, 2))
            .arg(parentName.mid(6, 2));
    }

    return fi.lastModified().toString(QStringLiteral("yyyy.MM.dd"));
}

} // namespace

QStringList AhdRecordStore::recordRootPaths()
{
    return existingRecordRoots();
}

bool AhdRecordStore::hasRecordStorage()
{
    const QStringList roots = existingRecordRoots();
    const bool present = !roots.isEmpty();
    qDebug() << "[AhdRecordStore] hasRecordStorage:" << present << "roots:" << roots;
    return present;
}

QStringList AhdRecordStore::listDateFolders()
{
    QSet<QString> dates;
    QStringList all;
    for (const QString &root : existingRecordRoots()) {
        collectMp4(root, &all);
    }
    for (const QString &path : all) {
        dates.insert(dateKeyFromFileInfo(QFileInfo(path)));
    }
    QStringList sorted = dates.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<QString>());
    return sorted;
}

QStringList AhdRecordStore::listVideoFilesForDate(const QString &dateKey)
{
    QStringList matched;
    QStringList all;
    for (const QString &root : existingRecordRoots()) {
        collectMp4(root, &all);
    }
    for (const QString &path : all) {
        if (dateKeyFromFileInfo(QFileInfo(path)) == dateKey) {
            matched.append(path);
        }
    }
    std::sort(matched.begin(), matched.end());
    return matched;
}

QString AhdRecordStore::displayNameForFile(const QString &filePath)
{
    const QFileInfo fi(filePath);
    QString displayName;
    if (parseRecordTimestamp(fi.completeBaseName(), nullptr, &displayName)) {
        return displayName;
    }

    QString name = fi.completeBaseName();
    name.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (name.size() > 10 && name.at(4) == QLatin1Char('.') && name.at(7) == QLatin1Char('.')) {
        return name;
    }
    const QDateTime dt = fi.lastModified();
    return dt.toString(QStringLiteral("yyyy.MM.dd_hhmmss"));
}

bool AhdRecordStore::formatStorage(QString *errorMessage)
{
    const QStringList roots = existingRecordRoots();
    if (roots.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未检测到存储卡");
        }
        return false;
    }

    int removed = 0;
    for (const QString &root : roots) {
        QStringList files;
        collectMp4(root, &files);
        for (const QString &path : files) {
            if (QFile::remove(path)) {
                ++removed;
            }
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("已清除 %1 个录像文件").arg(removed);
    }
    return true;
}
