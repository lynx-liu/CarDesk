#ifndef TOUCHCLICKSOUND_H
#define TOUCHCLICKSOUND_H

/** 点屏音：进程内 ALSA 播放；占用期间媒体不得抢设备。 */
namespace TouchClickSound {

/** 当前是否正占用 PCM（点屏音播放中）。 */
bool isBusy();

/**
 * 播放点屏音（level: 1=轻 2=重；其它视为关）。
 * 车机：后台线程写 PCM，立即返回，避免阻塞 GUI 导致连点/连删卡顿；
 *       已占用时直接返回 false。
 * PC：尽力用 QSoundEffect，失败则静默。
 */
bool play(int level);

} // namespace TouchClickSound

#endif
