#include "backlight.h"

#include <QByteArray>
#include <QString>
#include <fcntl.h>
#include <unistd.h>

namespace Backlight {

// 缓存最后一次 set() 写入的值，避免重复 sysfs 读取
static int s_cache = -1;

static int readFileValue(const char *path)
{
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[32] = {};
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return -1;
    bool ok = false;
    int v = QString::fromLatin1(buf, static_cast<int>(n)).trimmed().toInt(&ok);
    return ok ? v : -1;
}

static int tryReadPaths(const char *paths[])
{
    for (int i = 0; paths[i]; ++i) {
        int v = readFileValue(paths[i]);
        if (v >= 0) return v;
    }
    return -1;
}

static void writeDispDbg(const char *path, const QByteArray &data)
{
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t written = ::write(fd, data.constData(), static_cast<size_t>(data.size()));
        (void)written;
        ::close(fd);
    }
}

static int readDispDbgBrightness()
{
    writeDispDbg("/sys/kernel/debug/dispdbg/name", QByteArrayLiteral("lcd0\n"));
    writeDispDbg("/sys/kernel/debug/dispdbg/command", QByteArrayLiteral("getbl\n"));
    writeDispDbg("/sys/kernel/debug/dispdbg/start", QByteArrayLiteral("1\n"));
    return readFileValue("/sys/kernel/debug/dispdbg/param");
}

static int readFromSysfs()
{
    const char *paths[] = {
        "/sys/class/disp/disp/attr/lcd_bl",
        "/sys/class/backlight/backlight/brightness",
        "/sys/class/backlight/backlight/actual_brightness",
        "/sys/class/backlight/pwm-backlight/brightness",
        "/sys/class/backlight/pwm-backlight/actual_brightness",
        nullptr
    };
    int v = tryReadPaths(paths);
    if (v >= 0) return v;
    return readDispDbgBrightness();
}

int get()
{
    if (s_cache > 0) return s_cache;
    int v = readFromSysfs();
    s_cache = (v >= 0) ? v : 180;
    return s_cache;
}

void set(int value)
{
    s_cache = value;
    auto dbgWrite = [](const char *path, const QByteArray &data) {
        int fd = ::open(path, O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t written = ::write(fd, data.constData(), static_cast<size_t>(data.size()));
            (void)written;
            ::close(fd);
        }
    };
    dbgWrite("/sys/kernel/debug/dispdbg/name",    QByteArrayLiteral("lcd0\n"));
    dbgWrite("/sys/kernel/debug/dispdbg/command", QByteArrayLiteral("setbl\n"));
    dbgWrite("/sys/kernel/debug/dispdbg/param",   QByteArray::number(value) + "\n");
    dbgWrite("/sys/kernel/debug/dispdbg/start",   QByteArrayLiteral("1\n"));
}

int sliderToBacklight(int sliderVal)
{
    // slider 0 → 10（最暗可见），slider 100 → 255（最亮）
    const int v = 10 + sliderVal * 245 / 100;
    return v < 10 ? 10 : (v > 255 ? 255 : v);
}

int backlightToSlider(int blVal)
{
    if (blVal <= 10) return 0;
    const int v = ((blVal - 10) * 100 + 122) / 245;  // +122 = 245/2，四舍五入
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

} // namespace Backlight
