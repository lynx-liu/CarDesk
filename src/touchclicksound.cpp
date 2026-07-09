#include "touchclicksound.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QApplication>
#include <QtGlobal>
#include <atomic>
#include <cstring>

#include "t507sdkbridge.h"

#ifdef CAR_DESK_USE_T507_SDK
#include <alsa/asoundlib.h>
#else
#include <QSoundEffect>
#include <QUrl>
#endif

namespace TouchClickSound {
namespace {

std::atomic_bool g_busy{false};

struct WavPcm {
    unsigned int channels = 0;
    unsigned int rate = 0;
    unsigned int bits = 16;
    QByteArray pcm;
};

bool parseWavPcm(const QByteArray &fileData, WavPcm *out)
{
    if (!out || fileData.size() < 44) {
        return false;
    }
    const uchar *p = reinterpret_cast<const uchar *>(fileData.constData());
    auto u32 = [&](int off) -> quint32 {
        return quint32(p[off]) | (quint32(p[off + 1]) << 8)
                | (quint32(p[off + 2]) << 16) | (quint32(p[off + 3]) << 24);
    };
    auto u16 = [&](int off) -> quint16 {
        return quint16(p[off]) | (quint16(p[off + 1]) << 8);
    };
    if (u32(0) != 0x46464952u || u32(8) != 0x45564157u) { // RIFF / WAVE
        return false;
    }

    int off = 12;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 rate = 0;
    quint16 bits = 0;
    QByteArray pcm;
    while (off + 8 <= fileData.size()) {
        const quint32 id = u32(off);
        const quint32 sz = u32(off + 4);
        const int dataOff = off + 8;
        if (dataOff + int(sz) > fileData.size()) {
            break;
        }
        if (id == 0x20746d66u) { // fmt
            if (sz < 16) {
                return false;
            }
            audioFormat = u16(dataOff);
            channels = u16(dataOff + 2);
            rate = u32(dataOff + 4);
            bits = u16(dataOff + 14);
        } else if (id == 0x61746164u) { // data
            pcm = fileData.mid(dataOff, int(sz));
        }
        off = dataOff + int((sz + 1u) & ~1u);
    }
    if (audioFormat != 1 || channels == 0 || rate == 0 || bits == 0 || pcm.isEmpty()) {
        return false;
    }
    out->channels = channels;
    out->rate = rate;
    out->bits = bits;
    out->pcm = pcm;
    return true;
}

/** 去掉首尾近静音，并截到约 120ms，缩短占用 ALSA 时间。 */
void trimClickPcm(WavPcm *wav)
{
    if (!wav || wav->bits != 16 || wav->channels == 0 || wav->pcm.isEmpty()) {
        return;
    }
    const int frameBytes = int(wav->channels) * 2;
    if (frameBytes <= 0 || (wav->pcm.size() % frameBytes) != 0) {
        return;
    }
    const qint16 *samples = reinterpret_cast<const qint16 *>(wav->pcm.constData());
    const int frames = wav->pcm.size() / frameBytes;
    const int thr = 200;
    int first = -1;
    int last = -1;
    for (int f = 0; f < frames; ++f) {
        int peak = 0;
        for (unsigned c = 0; c < wav->channels; ++c) {
            const int v = qAbs(int(samples[f * int(wav->channels) + int(c)]));
            if (v > peak) {
                peak = v;
            }
        }
        if (peak >= thr) {
            if (first < 0) {
                first = f;
            }
            last = f;
        }
    }
    if (first < 0) {
        return;
    }
    const int maxFrames = int(wav->rate * 120 / 1000); // ≤120ms
    int end = last + 1;
    if (end - first > maxFrames) {
        end = first + maxFrames;
    }
    wav->pcm = wav->pcm.mid(first * frameBytes, (end - first) * frameBytes);
}

const WavPcm *cachedWav(int level)
{
    static QMutex mutex;
    static WavPcm soft;
    static WavPcm loud;
    static bool softOk = false;
    static bool loudOk = false;
    static bool softTried = false;
    static bool loudTried = false;

    QMutexLocker lock(&mutex);
    WavPcm *slot = (level == 1) ? &soft : &loud;
    bool *ok = (level == 1) ? &softOk : &loudOk;
    bool *tried = (level == 1) ? &softTried : &loudTried;
    if (*ok) {
        return slot;
    }
    if (*tried) {
        return nullptr;
    }
    *tried = true;

    const QString resource = (level == 1)
            ? QStringLiteral(":/sound/click_soft.wav")
            : QStringLiteral(":/sound/click_loud.wav");
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[ClickSound] missing resource" << resource;
        return nullptr;
    }
    const QByteArray data = f.readAll();
    if (!parseWavPcm(data, slot)) {
        qWarning() << "[ClickSound] bad wav" << resource << "size=" << data.size();
        return nullptr;
    }
    trimClickPcm(slot);
    if (slot->pcm.isEmpty()) {
        qWarning() << "[ClickSound] empty pcm after trim" << resource;
        return nullptr;
    }
    *ok = true;
    qDebug() << "[ClickSound] cached level=" << level
             << "ch=" << slot->channels << "rate=" << slot->rate
             << "bytes=" << slot->pcm.size()
             << "ms≈" << (slot->rate ? (slot->pcm.size() * 1000)
                                        / (int(slot->channels) * 2 * int(slot->rate))
                                      : 0);
    return slot;
}

#ifdef CAR_DESK_USE_T507_SDK
bool playPcmWithAlsa(const WavPcm &wav)
{
    if (wav.bits != 16) {
        qWarning() << "[ClickSound] only 16-bit PCM supported, bits=" << wav.bits;
        return false;
    }

    // 确保功放切到 SoC 媒体声道（与音乐/视频一致），否则可能听不到。
    T507SdkBridge::setAudioSource(false);

    snd_pcm_t *handle = nullptr;
    // 优先 default（走 asound.conf 路由）；失败再试 hw:0,0（与 tinyplay 默认一致）。
    const char *devices[] = {"default", "hw:0,0", "plughw:0,0"};
    int openRc = -1;
    const char *opened = nullptr;
    for (const char *dev : devices) {
        openRc = snd_pcm_open(&handle, dev, SND_PCM_STREAM_PLAYBACK, 0);
        if (openRc >= 0) {
            opened = dev;
            break;
        }
        handle = nullptr;
    }
    if (openRc < 0 || !handle) {
        qWarning() << "[ClickSound] snd_pcm_open failed:" << snd_strerror(openRc);
        return false;
    }

    const int setRc = snd_pcm_set_params(
        handle,
        SND_PCM_FORMAT_S16_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        wav.channels,
        wav.rate,
        1,          // soft resample
        100000);    // 100ms latency
    if (setRc < 0) {
        qWarning() << "[ClickSound] snd_pcm_set_params failed on" << opened
                   << ":" << snd_strerror(setRc)
                   << "ch=" << wav.channels << "rate=" << wav.rate;
        snd_pcm_close(handle);
        return false;
    }

    const int frameBytes = int(wav.channels) * 2;
    const qint16 *samples = reinterpret_cast<const qint16 *>(wav.pcm.constData());
    snd_pcm_uframes_t framesLeft = static_cast<snd_pcm_uframes_t>(wav.pcm.size() / frameBytes);
    bool ok = true;

    while (framesLeft > 0) {
        const snd_pcm_sframes_t written = snd_pcm_writei(handle, samples, framesLeft);
        if (written == -EPIPE) {
            snd_pcm_prepare(handle);
            continue;
        }
        if (written < 0) {
            qWarning() << "[ClickSound] snd_pcm_writei failed:" << snd_strerror(int(written));
            ok = false;
            break;
        }
        samples += written * int(wav.channels);
        framesLeft -= static_cast<snd_pcm_uframes_t>(written);
    }

    if (ok) {
        snd_pcm_drain(handle);
    }
    snd_pcm_close(handle);
    qDebug() << "[ClickSound] played via ALSA device=" << opened << "ok=" << ok;
    return ok;
}
#endif

} // namespace

bool isBusy()
{
    return g_busy.load(std::memory_order_acquire);
}

bool play(int level)
{
    if (level <= 0) {
        return false;
    }
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    bool ok = false;
#ifdef CAR_DESK_USE_T507_SDK
    const WavPcm *wav = cachedWav(level);
    if (wav) {
        ok = playPcmWithAlsa(*wav);
    } else {
        qWarning() << "[ClickSound] no wav for level=" << level;
    }
#else
    static QSoundEffect *fxSoft = nullptr;
    static QSoundEffect *fxLoud = nullptr;
    QSoundEffect *&fx = (level == 1) ? fxSoft : fxLoud;
    if (!fx) {
        fx = new QSoundEffect(qApp);
        fx->setSource(QUrl(level == 1
                               ? QStringLiteral("qrc:/sound/click_soft.wav")
                               : QStringLiteral("qrc:/sound/click_loud.wav")));
        fx->setVolume(1.0);
    }
    if (fx->status() != QSoundEffect::Error) {
        fx->play();
        ok = true;
    }
    g_busy.store(false, std::memory_order_release);
    return ok;
#endif

    g_busy.store(false, std::memory_order_release);
    return ok;
}

} // namespace TouchClickSound
