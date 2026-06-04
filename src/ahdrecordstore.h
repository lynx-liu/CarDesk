#ifndef AHDRECORDSTORE_H
#define AHDRECORDSTORE_H

#include <QStringList>

// 扫描 SDK 录像目录：TF 在 /mnt/sdcard/<子目录>/（子目录已挂载或非空视为有卡）
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
