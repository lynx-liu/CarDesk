#ifndef APPLOG_H
#define APPLOG_H

// 为 qDebug/qWarning/qCritical 等输出统一加日期时间前缀
void installAppLogHandler();

// 启动时打印编译时间（需 CarDesk.pro 注入 APP_BUILD_DATETIME）
void logBuildInfo();

#endif // APPLOG_H
