#ifndef AHDRECORDSTORE_H
#define AHDRECORDSTORE_H

#include <QStringList>

// SDK 录像目录：路径由 TfCardMonitor 检测后写入缓存，此处只读
class AhdRecordStore {
public:
    static QStringList recordRootPaths();
    static bool hasRecordStorage();
    /** 仅 TfCardMonitor 在 rescan 后调用 */
    static void updateStorageCache(bool present, const QStringList &roots);

    static QStringList listDateFolders();
    static QStringList listVideoFilesForDate(const QString &dateKey);
    static QString dateKeyForFile(const QString &filePath);
    static QStringList filterExistingFiles(const QStringList &paths);
    static QString displayNameForFile(const QString &filePath);

    static bool formatStorage(QString *errorMessage);
};

#endif // AHDRECORDSTORE_H
