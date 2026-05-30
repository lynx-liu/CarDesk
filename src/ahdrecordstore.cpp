#include "ahdrecordstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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

bool shouldSkipDirName(const QString &name)
{
    return name.isEmpty() || name.startsWith(QLatin1Char('.'));
}

// SDK 录像名：{idx}-{yyyyMMdd}_{HHmmss}_{suffix}.mp4，例如 0-20240529_183024_front.mp4
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
