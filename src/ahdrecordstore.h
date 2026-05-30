#ifndef AHDRECORDSTORE_H
#define AHDRECORDSTORE_H

#include <QStringList>

// 扫描 SDK 录像目录：TF 卡挂载在 /mnt/sdcard/<volume>/ 下
class AhdRecordStore {
public:
    static QStringList recordRootPaths();
    static bool hasRecordStorage();

    static QStringList listDateFolders();
    static QStringList listVideoFilesForDate(const QString &dateKey);
    static QString displayNameForFile(const QString &filePath);

    static bool formatStorage(QString *errorMessage);
};

#endif // AHDRECORDSTORE_H
