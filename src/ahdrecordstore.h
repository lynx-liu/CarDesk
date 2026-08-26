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

    /** 全部录像文件，按 SDK 实际写入顺序（最旧→最新），不依赖文件名时间 */
    static QStringList listAllVideoFilesOrdered();
    static QStringList filterExistingFiles(const QStringList &paths);
    static QString displayNameForFile(const QString &filePath);

    static bool formatStorage(QString *errorMessage);
};

#endif // AHDRECORDSTORE_H
