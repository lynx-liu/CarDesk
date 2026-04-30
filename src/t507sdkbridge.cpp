#include "t507sdkbridge.h"
#include <QMap>
#include <QDebug>
#include <QStorageInfo>
#ifdef CAR_DESK_DEVICE_CARUNIT
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

// TM2313 ioctl 命令（来自内核驱动 tm2313.h，用户空间直接引用数值）
static const int TM2313_LOUDNESS               = 10000; // arg: 1=开, 0=关
static const int TM2313_VOLUME_OUT_GAIN        = 10001; // arg: 0..63, 0=max gain, 63=min gain
static const int TM2313_TREBLE                 = 10002; // arg: 0-15，TDA7313编码(0=+14dB,7=0dB,8=-2dB,15=-14dB)
static const int TM2313_BASS                   = 10003; // arg: 同上
static const int TM2313_MUTE_ENABLE            = 10008; // arg: 1=取消静音, 0=静音
static const int TM2313_INPUT_SWITCH_STEREO_1  = 10011; // 切换到 IN1（收音机 / TEA685x 模拟输出）
static const int TM2313_INPUT_SWITCH_STEREO_2  = 10012; // 切换到 IN2（媒体声道 / SoC DAC，默认）

static int volumeGainFromLevel(int level)
{
    const int clamped = qBound(0, level, 10);
    if (clamped == 0) {
        return 0; // level0 直接静音，不使用 gain 表
    }
    // 使用旧音量等级映射：level1..level10 对应 gain 值
    // 1:0x29  2:0x25  3:0x20  4:0x1A  5:0x15  6:0x10  7:0x09  8:0x05  9:0x00 10:0x00
    static const int gainTable[10] = {0x29, 0x25, 0x20, 0x1A, 0x15, 0x10, 0x09, 0x05, 0x00, 0x00};
    return gainTable[clamped - 1];
}

// 声场预设表：{treble(0-15), bass(0-15), loudness(0/1)}
// 编码参考 TDA7313 兼容规范：0=+14dB, 1=+12dB ... 7=0dB(flat), 8=-2dB ... 15=-14dB
struct Tm2313Preset { int treble; int bass; int loudness; };

static const QMap<QString, Tm2313Preset> &soundPresets()
{
    static const QMap<QString, Tm2313Preset> presets = {
        // 立体声：完全平坦，适合原声音乐
        {QStringLiteral("立体声"),   {7,  7,  0}},
        // 环绕声：高低音轻微提升，增加空间感
        {QStringLiteral("环绕声"),   {5,  5,  0}},
        // 低音增强：低音大幅提升（+10dB），响度补偿开
        {QStringLiteral("低音增强"), {7,  2,  1}},
        // 高音增强：高音提升（+10dB），低音平坦
        {QStringLiteral("高音增强"), {2,  7,  0}},
        // 平衡音：与立体声相同，强调左右均衡
        {QStringLiteral("平衡音"),   {7,  7,  0}},
        // 音场定位：高低音适度提升，增强方向感
        {QStringLiteral("音场定位"), {5,  4,  0}},
        // 降噪音效：高低音轻微衰减，减少噪声感
        {QStringLiteral("降噪音效"), {9,  9,  0}},
        // 虚拟声场：高低音较大提升 + 响度，营造环绕感
        {QStringLiteral("虚拟声场"), {4,  3,  1}},
        // 响度补偿：平坦响度开，低音微提，适合低音量聆听
        {QStringLiteral("响度补偿"), {7,  6,  1}},
    };
    return presets;
}

void T507SdkBridge::setSoundMode(const QString &modeName)
{
    const auto &presets = soundPresets();
    if (!presets.contains(modeName)) {
        qWarning() << "[TM2313] Unknown sound mode:" << modeName;
        return;
    }
    const Tm2313Preset &p = presets[modeName];
    qDebug() << "[TM2313] setSoundMode:" << modeName
             << "treble=" << p.treble << "bass=" << p.bass << "loudness=" << p.loudness;

#ifdef CAR_DESK_DEVICE_CARUNIT
    int fd = ::open("/dev/tm2313", O_RDWR);
    if (fd < 0) {
        qWarning() << "[TM2313] Cannot open /dev/tm2313";
        return;
    }
    ::ioctl(fd, TM2313_TREBLE,   (unsigned long)p.treble);
    ::ioctl(fd, TM2313_BASS,     (unsigned long)p.bass);
    ::ioctl(fd, TM2313_LOUDNESS, (unsigned long)p.loudness);
    ::close(fd);
#else
    Q_UNUSED(TM2313_LOUDNESS)
    Q_UNUSED(TM2313_TREBLE)
    Q_UNUSED(TM2313_BASS)
#endif
}

void T507SdkBridge::setAudioSource(bool radioMode)
{
    qDebug() << "[TM2313] setAudioSource:" << (radioMode ? "radio(STEREO_1/IN1)" : "media(STEREO_2/IN2)");
#ifdef CAR_DESK_DEVICE_CARUNIT
    int fd = ::open("/dev/tm2313", O_RDWR);
    if (fd < 0) {
        qWarning() << "[TM2313] Cannot open /dev/tm2313";
        return;
    }
    // radioMode=true  → STEREO_1 (0x40, IN1): TEA685x FM/AM 模拟输出
    // radioMode=false → STEREO_2 (0x41, IN2): SoC DAC 媒体声道（默认）
    ::ioctl(fd, radioMode ? TM2313_INPUT_SWITCH_STEREO_1 : TM2313_INPUT_SWITCH_STEREO_2, 0);
    ::ioctl(fd, TM2313_MUTE_ENABLE, 1); // 取消静音，确保切换后有声音
    ::close(fd);
#else
    Q_UNUSED(radioMode)
    Q_UNUSED(TM2313_INPUT_SWITCH_STEREO_1)
    Q_UNUSED(TM2313_INPUT_SWITCH_STEREO_2)
    Q_UNUSED(TM2313_MUTE_ENABLE)
#endif
}

void T507SdkBridge::setVolumeLevel(int level)
{
    const int clampedLevel = qBound(0, level, 10);
    const int gainValue = volumeGainFromLevel(clampedLevel);
    qDebug() << "[TM2313] setVolumeLevel:" << clampedLevel << "gainValue:" << gainValue;
#ifdef CAR_DESK_DEVICE_CARUNIT
    int fd = ::open("/dev/tm2313", O_RDWR);
    if (fd < 0) {
        qWarning() << "[TM2313] Cannot open /dev/tm2313";
        return;
    }
    if (clampedLevel == 0) {
        qDebug() << "[TM2313] mute";
        ::ioctl(fd, TM2313_MUTE_ENABLE, 0);
    } else {
        qDebug() << "[TM2313] unmute";
        ::ioctl(fd, TM2313_MUTE_ENABLE, 1);
        ::ioctl(fd, TM2313_VOLUME_OUT_GAIN, (unsigned long)gainValue);
    }
    ::close(fd);
#else
    Q_UNUSED(clampedLevel)
    Q_UNUSED(gainValue)
    Q_UNUSED(TM2313_VOLUME_OUT_GAIN)
    Q_UNUSED(TM2313_MUTE_ENABLE)
#endif
}

QStringList T507SdkBridge::ahdVideoNodes()
{
    // Based on T507 board debug document: single-channel AHD test uses /dev/video2-5.
    return {QStringLiteral("/dev/video2"), QStringLiteral("/dev/video3"), QStringLiteral("/dev/video4"), QStringLiteral("/dev/video5")};
}

QString T507SdkBridge::ahdChannelName(int index)
{
    static const QStringList channels = {
        QStringLiteral("AHD1（通道1）"),
        QStringLiteral("AHD2（通道2）"),
        QStringLiteral("AHD3（通道3）"),
        QStringLiteral("AHD4（通道4）")
    };
    return channels[index % channels.size()];
}

QString T507SdkBridge::ahdTestCommand(int index)
{
    const int sdkNode = (index % 4) + 2;
    return QStringLiteral("sdktest 1 %1").arg(sdkNode);
}

QString T507SdkBridge::usbMountPath()
{
    const QString base = QStringLiteral("/mnt/usb");
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady())
            continue;

        const QString root = storage.rootPath();
        if (root == base || root.startsWith(base + QStringLiteral("/"))) {
            return root;
        }
    }
    return QStringLiteral("/mnt/usb/sda1");
}

QString T507SdkBridge::tfMountPath()
{
    return QStringLiteral("/mnt/sdcard/mmcblk1p1");
}

QString T507SdkBridge::emmcUdiskMountPath()
{
    return QStringLiteral("/mnt/UDISK");
}