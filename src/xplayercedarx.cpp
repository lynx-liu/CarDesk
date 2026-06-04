#include "xplayercedarx.h"

#include "ahdrecordstore.h"

#include <QFileInfo>

bool cedarxIsAhdRecordVideoPath(const QString &path)
{
    const QString absPath = QFileInfo(path).absoluteFilePath();
    if (absPath.isEmpty()) {
        return false;
    }

    const QStringList roots = AhdRecordStore::recordRootPaths();
    for (const QString &root : roots) {
        const QString absRoot = QFileInfo(root).absoluteFilePath();
        if (absRoot.isEmpty()) {
            continue;
        }
        if (absPath.startsWith(absRoot + QLatin1Char('/')) || absPath == absRoot) {
            return true;
        }
    }
    return false;
}

#ifdef CAR_DESK_USE_T507_SDK

#include <player.h>

// 与 WSDK cedarx/xplayer/xplayer.c 中 PlayerContext 前两字段布局一致
typedef struct CedarXPlayerContext {
    void *mMessageQueue;
    Player *mPlayer;
} CedarXPlayerContext;

void cedarxXPlayerSetDiscardAudio(XPlayer *player, int discard)
{
    if (!player) {
        return;
    }
    CedarXPlayerContext *ctx = reinterpret_cast<CedarXPlayerContext *>(player);
    if (!ctx->mPlayer) {
        return;
    }
    PlayerSetDiscardAudio(ctx->mPlayer, discard ? 1 : 0);
}

#endif
