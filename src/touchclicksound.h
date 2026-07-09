#ifndef TOUCHCLICKSOUND_H
#define TOUCHCLICKSOUND_H

/** 点屏音：进程内 tinyalsa 播放；占用期间媒体不得抢 ALSA。 */
namespace TouchClickSound {

/** 当前是否正占用 PCM（点屏音播放中）。 */
bool isBusy();

/**
 * 播放点屏音（level: 1=轻 2=重；其它视为关）。
 * 车机：本进程阻塞写 PCM 直至结束；已占用或媒体占用时直接返回 false。
 * PC：尽力用 QSoundEffect，失败则静默。
 */
bool play(int level);

} // namespace TouchClickSound

#endif
