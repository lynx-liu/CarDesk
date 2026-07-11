#ifndef TOUCHCLICKSOUND_H
#define TOUCHCLICKSOUND_H

/** 点屏音：专用音频线程播短 PCM（类似 SoundPool），不阻塞 UI。 */
namespace TouchClickSound {

/** 当前是否正占用 PCM（点屏音播放中）。媒体起播前可据此延后。 */
bool isBusy();

/**
 * 请求播放点屏音（level: 1=柔和 2=响亮；其它视为关）。
 * 立即返回：真正的 ALSA 开/写/关都在专用音频线程完成。
 * 正在播时再点 → 丢弃本次（不叠音、不排队），与常见车机/手机策略一致。
 */
bool play(int level);

/** 进程退出前停止音频线程。 */
void shutdown();

} // namespace TouchClickSound

#endif
