#ifndef XPLAYERCEDARX_H
#define XPLAYERCEDARX_H

#include <QString>

/** TF 行车录像路径（含空音轨 MP4），与 U 盘普通视频区分 */
bool cedarxIsAhdRecordVideoPath(const QString &path);

#ifdef CAR_DESK_USE_T507_SDK
#include <xplayer.h>
/** 跳过首帧音画同步等待（PlayerSetDiscardAudio），仅用于无有效音频数据的录像 */
void cedarxXPlayerSetDiscardAudio(XPlayer *player, int discard);
#endif

#endif // XPLAYERCEDARX_H
