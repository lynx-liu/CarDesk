#include "appsignals.h"

// AppSignals 是 header-only 单例，此 .cpp 存在仅为确保 moc 生成的
// appsignals.moc 被正确编译进 target（Q_OBJECT 宏要求 .h 出现在 HEADERS 中）。
