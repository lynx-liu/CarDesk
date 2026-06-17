#include "ahdrecordstore.h"
#include "tfcarddetect.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

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

bool parseRecordTimestamp(const QString &baseName, QString *dateKey)
{
    static const QRegularExpression re(
        QStringLiteral(R"((\d{4})(\d{2})(\d{2})_(\d{6}))"));
    const QRegularExpressionMatch match = re.match(baseName);
    if (!match.hasMatch()) {
        return false;
    }
    if (dateKey) {
        *dateKey = QStringLiteral("%1.%2.%3")
                       .arg(match.captured(1), match.captured(2), match.captured(3));
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
    if (parseRecordTimestamp(baseName, &dateKey)) {
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

quint64 recordFileTimeSortKey(const QString &path)
{
    static const QRegularExpression re(
        QStringLiteral(R"((\d{4})(\d{2})(\d{2})_(\d{6}))"));
    const QRegularExpressionMatch match =
        re.match(QFileInfo(path).completeBaseName());
    if (match.hasMatch()) {
        return match.captured(1).toUInt() * 10000000000ULL + match.captured(2).toUInt() * 100000000ULL
               + match.captured(3).toUInt() * 1000000ULL + match.captured(4).toUInt();
    }
    return static_cast<quint64>(QFileInfo(path).lastModified().toSecsSinceEpoch());
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

QStringList AhdRecordStore::listDateFolders()
{
    QSet<QString> dates;
    QStringList all;
    for (const QString &root : cachedRecordRoots()) {
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
    for (const QString &root : cachedRecordRoots()) {
        collectMp4(root, &all);
    }
    for (const QString &path : all) {
        if (dateKeyFromFileInfo(QFileInfo(path)) == dateKey) {
            matched.append(path);
        }
    }
    std::sort(matched.begin(), matched.end(), [](const QString &a, const QString &b) {
        const quint64 ka = recordFileTimeSortKey(a);
        const quint64 kb = recordFileTimeSortKey(b);
        if (ka != kb) {
            return ka < kb;
        }
        return QFileInfo(a).fileName().compare(QFileInfo(b).fileName(), Qt::CaseInsensitive) < 0;
    });
    return matched;
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
