#include "ahdrecordstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

const char *kRecordRoots[] = {"/mnt/UDISK", "/mnt/sdcard/mmcblk1p1", "/mnt/sdcard"};

QStringList existingRecordRoots()
{
    QStringList roots;
    for (const char *path : kRecordRoots) {
        const QFileInfo info(QString::fromUtf8(path));
        if (info.exists() && info.isDir()) {
            roots.append(info.absoluteFilePath());
        }
    }
    return roots;
}

void collectMp4(const QString &dirPath, QStringList *out)
{
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir()) {
            collectMp4(fi.absoluteFilePath(), out);
        } else if (fi.isFile() && fi.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
            out->append(fi.absoluteFilePath());
        }
    }
}

QString dateKeyFromFileInfo(const QFileInfo &fi)
{
    const QString baseName = fi.completeBaseName();
    if (baseName.size() >= 10 && baseName.at(4) == QLatin1Char('.') && baseName.at(7) == QLatin1Char('.')) {
        return baseName.left(10);
    }
    const int us = baseName.indexOf(QLatin1Char('_'));
    if (us >= 8) {
        const QString head = baseName.left(us);
        if (head.size() >= 10 && head.at(4) == QLatin1Char('.') && head.at(7) == QLatin1Char('.')) {
            return head.left(10);
        }
        if (head.size() >= 8) {
            return QStringLiteral("%1.%2.%3")
                .arg(head.mid(0, 4))
                .arg(head.mid(4, 2))
                .arg(head.mid(6, 2));
        }
    }
    return fi.lastModified().toString(QStringLiteral("yyyy.MM.dd"));
}

} // namespace

QStringList AhdRecordStore::recordRootPaths()
{
    return existingRecordRoots();
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
