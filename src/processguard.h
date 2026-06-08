#ifndef PROCESSGUARD_H
#define PROCESSGUARD_H

#include <QString>

class ProcessGuard
{
public:
    static bool tryAcquireInstanceLock(QString *errorOut = nullptr);
    static void releaseInstanceLock();

    // 扫描 /proc，发现其他 CarDesk 实例（含 D 状态僵尸）则返回 true。
    static bool hasOtherCarDeskInstances(QString *detailOut = nullptr);

private:
    static int s_lockFd;
};

#endif
