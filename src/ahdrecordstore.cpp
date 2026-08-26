#include "ahdrecordstore.h"
#include "tfcarddetect.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace {

bool shouldSkipDirName(const QString &name)
{
    return name.isEmpty() || name.startsWith(QLatin1Char('.'));
}

struct StorageCache {
    QMutex mutex;
    bool present = false;
    QStringList roots;
};

StorageCache &storageCache()
{
    static StorageCache cache;
    return cache;
}

QStringList cachedRecordRoots()
{
    QMutexLocker lock(&storageCache().mutex);
    return storageCache().roots;
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

// SDK 录像文件名形如 "3-20260826_101512_CH1.mp4"，前缀数字是环形槽位号
int slotIndexFromPath(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    const int dash = name.indexOf(QLatin1Char('-'));
    if (dash <= 0) {
        return -1;
    }
    bool ok = false;
    const int idx = name.left(dash).toInt(&ok);
    return (ok && idx >= 0) ? idx : -1;
}

// SDK 环形写入指针（下一个要覆盖的槽位，即最旧文件的槽位），
// 由 sdk_camera 持久化在 /etc/dvrconfig.ini 的 [camera0] cur-fileidx，App 只读
int sdkRecordWriteIndex()
{
    QFile file(QStringLiteral("/etc/dvrconfig.ini"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }
    bool inCameraSection = false;
    while (!file.atEnd()) {
        const QString line = QString::fromLatin1(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inCameraSection = (line == QLatin1String("[camera0]"));
            continue;
        }
        if (!inCameraSection || !line.startsWith(QLatin1String("cur-fileidx"))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) {
            bool ok = false;
            const int idx = line.mid(eq + 1).trimmed().toInt(&ok);
            if (ok && idx >= 0) {
                return idx;
            }
        }
    }
    return 0;
}

} // namespace

void AhdRecordStore::updateStorageCache(bool present, const QStringList &roots)
{
    QMutexLocker lock(&storageCache().mutex);
    storageCache().present = present;
    storageCache().roots = roots;
}

QStringList AhdRecordStore::recordRootPaths()
{
    return cachedRecordRoots();
}

bool AhdRecordStore::hasRecordStorage()
{
    QMutexLocker lock(&storageCache().mutex);
    return storageCache().present;
}

QStringList AhdRecordStore::listAllVideoFilesOrdered()
{
    QStringList all;
    for (const QString &root : cachedRecordRoots()) {
        collectMp4(root, &all);
    }

    // 实际写入顺序：SDK 按槽位号环形覆盖，写入指针指向最旧的槽位；
    // 从指针处回绕展开即为“最旧→最新”，不依赖文件名里的时间（系统时间可能不准）
    const int writeIdx = sdkRecordWriteIndex();
    constexpr quint64 kWrap = 1000000ULL;
    auto orderKey = [writeIdx](const QString &path) -> quint64 {
        const int slot = slotIndexFromPath(path);
        if (slot < 0) {
            return kWrap * 2;  // 无槽位号的文件排在最后
        }
        return slot >= writeIdx ? quint64(slot - writeIdx)
                                : quint64(slot) + kWrap - quint64(writeIdx);
    };
    std::sort(all.begin(), all.end(), [&orderKey](const QString &a, const QString &b) {
        const quint64 ka = orderKey(a);
        const quint64 kb = orderKey(b);
        if (ka != kb) {
            return ka < kb;
        }
        return QFileInfo(a).fileName().compare(QFileInfo(b).fileName(), Qt::CaseInsensitive) < 0;
    });
    return all;
}

QStringList AhdRecordStore::filterExistingFiles(const QStringList &paths)
{
    QStringList existing;
    existing.reserve(paths.size());
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            existing.append(path);
        }
    }
    return existing;
}

QString AhdRecordStore::displayNameForFile(const QString &filePath)
{
    return QFileInfo(filePath).completeBaseName();
}

bool AhdRecordStore::formatStorage(QString *errorMessage)
{
    const QStringList roots = cachedRecordRoots();
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
    if (TfCardMonitor::instance()) {
        TfCardMonitor::instance()->rescanNow();
    }
    return true;
}
