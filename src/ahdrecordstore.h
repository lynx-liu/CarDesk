#ifndef AHDRECORDSTORE_H
#define AHDRECORDSTORE_H

#include <QStringList>

// 扫描 SDK 录像目录（StorageManager MOUNT_POINT 及常见 TF 卡路径）
class AhdRecordStore {
public:
    static QStringList recordRootPaths();

    static QStringList listDateFolders();
    static QStringList listVideoFilesForDate(const QString &dateKey);
    static QString displayNameForFile(const QString &filePath);

    static bool formatStorage(QString *errorMessage);
};

#endif // AHDRECORDSTORE_H
