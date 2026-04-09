#ifndef FAULTCODEDB_H
#define FAULTCODEDB_H

#include <QString>
#include <QVector>

struct FaultEntry {
    int     spn;
    int     fmi;
    QString dtc;   // DTC码 (如 P150301)
    QString desc;  // 中文描述
};

class FaultCodeDb {
public:
    // 根据控制器名称(ABS/EBS/BCM)和 SPN+FMI 查询中文描述，未找到返回空串
    static QString      lookup(const QString &controller, int spn, int fmi);
    // 根据控制器名称和 SPN+FMI 查询 DTC 码，未找到返回空串
    static QString      dtcCode(const QString &controller, int spn, int fmi);
    // 返回指定控制器的所有已知故障条目
    static QVector<FaultEntry> allFaults(const QString &controller);
};

#endif // FAULTCODEDB_H
