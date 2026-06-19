#include "ahdcamerapool.h"

#include "ahdrecordstore.h"
#include "ahdsettings.h"
#include "appsettings.h"
#include "appsignals.h"
#include "processguard.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <unistd.h>
#include <QProcessEnvironment>
#include <QThread>
#include <QVector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef CAR_DESK_USE_T507_SDK

#include <pthread.h>
#include <signal.h>

#include <sdklog.h>

#include "CameraFileCfg.h"
#include "CameraHardware2.h"
#include "Display.h"
#include "DvrFactory.h"
#include "DvrRecordManager.h"
#include "type_camera.h"

using namespace android;

AhdCameraPool *AhdCameraPool::s_activePool = nullptr;

namespace {

static constexpr int kChannelStartDelayMs = 300;
static constexpr int kHwOverlayHideDelayMs = 2500;
static constexpr int kColdStartWarmupMs = 1500;
static constexpr int kFactoryCreateRetries = 3;
static constexpr int kFactoryRetryDelayMs = 1000;
static constexpr char kAhdSessionMarkerPath[] = "/tmp/cardesk_ahd_active";

static bool g_dvrManagerInited = false;
static bool g_sdkRuntimePrepared = false;
static bool g_displayInited = false;
static bool g_uncleanHardwareRecovery = false;
static bool g_recordStorageAvailable = false;
static qint64 g_lastStoreErrNotifyMs = 0;

void prepareSdkRuntimeOnce()
{
    if (g_sdkRuntimePrepared) {
        return;
    }
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGIO);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    sdk_log_setlevel(0);
    g_sdkRuntimePrepared = true;
    qDebug() << "[Ahd] SDK runtime prepared (SIGIO blocked, log level=0)";
}

void markAhdSessionActive()
{
    QFile marker(kAhdSessionMarkerPath);
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        marker.write(QByteArray::number(getpid()));
        marker.close();
    }
}

void clearAhdSessionMarker()
{
    QFile::remove(kAhdSessionMarkerPath);
}

bool needsColdStartWarmup()
{
    QFile marker(kAhdSessionMarkerPath);
    if (!marker.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool ok = false;
    const qint64 pid = marker.readAll().trimmed().toLongLong(&ok);
    marker.close();
    return ok && pid != static_cast<qint64>(getpid());
}

void performHardwareRecovery(const char *reason)
{
    if (!g_uncleanHardwareRecovery) {
        return;
    }
    if (!qEnvironmentVariableIsSet("CARDESK_AHD_RESET_VIDEO")) {
        qWarning() << "[Ahd] hardware recovery:" << reason << ", warmup" << kColdStartWarmupMs
                   << "ms (set CARDESK_AHD_RESET_VIDEO=1 to fuser -k video nodes)";
        QThread::msleep(kColdStartWarmupMs);
        return;
    }
    static const char *kVideoNodes[] = {"/dev/video2", "/dev/video3", "/dev/video4",
                                        "/dev/video5"};
    qWarning() << "[Ahd] hardware recovery with video reset:" << reason;
    for (const char *node : kVideoNodes) {
        const QByteArray cmd = QByteArray("fuser -k ") + node + " 2>/dev/null";
        ::system(cmd.constData());
    }
    QThread::msleep(kColdStartWarmupMs);
}

void detectUncleanShutdown()
{
    if (needsColdStartWarmup()) {
        g_uncleanHardwareRecovery = true;
        qWarning() << "[Ahd] unclean previous AHD session (stale pid marker)";
    }
}

void noteHardwareRecoveryDone()
{
    g_uncleanHardwareRecovery = false;
    clearAhdSessionMarker();
}

int poolSlotForCameraId(int cameraId)
{
    if (cameraId == 360 || cameraId == 180) {
        return 0;
    }
    if (cameraId >= AhdCameraPool::kTvdDevIdStart
        && cameraId < AhdCameraPool::kTvdDevIdStart + AhdCameraPool::kChannelCount) {
        return cameraId - AhdCameraPool::kTvdDevIdStart;
    }
    return 0;
}

QVector<int> cameraIdsForLayout(const AhdLayoutSpec &spec)
{
    QVector<int> ids;
    switch (spec.mode) {
    case 360:
        // 应用层四分屏：四路独立预览 + Qt 布局合成，不依赖 SDK Oview/MultiCameraCompose
        ids << (AhdCameraPool::kTvdDevIdStart + 0) << (AhdCameraPool::kTvdDevIdStart + 1)
            << (AhdCameraPool::kTvdDevIdStart + 2) << (AhdCameraPool::kTvdDevIdStart + 3);
        break;
    case 180:
        ids << (AhdCameraPool::kTvdDevIdStart + 2) << (AhdCameraPool::kTvdDevIdStart + 3);
        break;
    case 270:
        ids << (AhdCameraPool::kTvdDevIdStart + 1) << (AhdCameraPool::kTvdDevIdStart + 2)
            << (AhdCameraPool::kTvdDevIdStart + 3);
        break;
    case 271:
        ids << (AhdCameraPool::kTvdDevIdStart + 2) << (AhdCameraPool::kTvdDevIdStart + 0)
            << (AhdCameraPool::kTvdDevIdStart + 3);
        break;
    case 272:
        ids << (AhdCameraPool::kTvdDevIdStart + 0) << (AhdCameraPool::kTvdDevIdStart + 2)
            << (AhdCameraPool::kTvdDevIdStart + 3);
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        ids.append(spec.mode);
        break;
    default:
        ids.append(360);
        break;
    }
    return ids;
}

void poolNotifyCallback(int32_t msgType, int32_t ext1, int32_t ext2, void *user)
{
    Q_UNUSED(ext1);
    Q_UNUSED(ext2);
    Q_UNUSED(user);
    if (!AhdCameraPool::s_activePool) {
        return;
    }
    if ((msgType & CAMERA_MSG_DVR_STORE_ERR) != CAMERA_MSG_DVR_STORE_ERR) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - g_lastStoreErrNotifyMs < 300) {
        return;
    }
    g_lastStoreErrNotifyMs = nowMs;
    qWarning() << "[Ahd] CAMERA_MSG_DVR_STORE_ERR, stop storage recording";
    g_recordStorageAvailable = false;
    QMetaObject::invokeMethod(AhdCameraPool::s_activePool, "scheduleRecordingSync", Qt::QueuedConnection);
}

bool isPreviewFrameMsg(int32_t msgType)
{
    // Qt 实时预览只消费 PREVIEW_FRAME；VIDEO_FRAME 仅留给录像编码，避免录像水印叠到前台预览。
    return msgType == CAMERA_MSG_PREVIEW_FRAME;
}

void poolDataCallback(int32_t msgType, char *dataPtr, camera_frame_metadata_t *metadata, void *user)
{
    Q_UNUSED(metadata);
    if (!isPreviewFrameMsg(msgType) || !dataPtr || !user || !AhdCameraPool::s_activePool) {
        return;
    }
    AhdCameraPool::s_activePool->onPreviewFrameFromSdk(user, dataPtr);
}

void poolDataCallbackTimestamp(nsecs_t timestamp, int32_t msgType, char *dataPtr, void *user)
{
    Q_UNUSED(timestamp);
    if (!isPreviewFrameMsg(msgType) || !dataPtr || !user || !AhdCameraPool::s_activePool) {
        return;
    }
    AhdCameraPool::s_activePool->onPreviewFrameFromSdk(user, dataPtr);
}

status_t poolUsrDataCb(int32_t msgType, char *dataPtr, int dalen, void *user)
{
    Q_UNUSED(msgType);
    Q_UNUSED(dataPtr);
    Q_UNUSED(dalen);
    Q_UNUSED(user);
    return 0;
}

extern "C" int glRelease(void);
extern "C" int gldestory(void);

bool isFactory360Ready(dvr_factory *dvr)
{
    if (!dvr || dvr->mCameraId != 360) {
        return false;
    }
    if (!dvr->m360CameraManager || !dvr->m360Hardware) {
        return false;
    }
    for (int i = 0; i < NUM_OF_360CAMERAS; ++i) {
        if (!dvr->m360Hardware[i]) {
            return false;
        }
    }
    return true;
}

bool isFactoryReady(dvr_factory *dvr, int cameraId)
{
    if (!dvr) {
        return false;
    }
    if (cameraId == 360) {
        return isFactory360Ready(dvr);
    }
    return dvr->mHardwareCameras != nullptr;
}

void stopDvrAudioCapture(dvr_factory *dvr)
{
    if (dvr && dvr->mAudioCap != nullptr) {
        dvr->mAudioCap->stopCapture(dvr->mAudioHdl);
    }
}

// 仅停录像写盘管线（VIDEO_FRAME 编码/音频/mux），不动 startPriview/PREVIEW_FRAME。
void stopDvrStoragePipeline(dvr_factory *dvr)
{
    if (!dvr) {
        return;
    }
    stopDvrAudioCapture(dvr);
    if (dvr->mHardwareCameras) {
        dvr->mHardwareCameras->disableMsgType(CAMERA_MSG_VIDEO_FRAME);
        dvr->mHardwareCameras->stopRecording();
    } else if (dvr->m360Hardware && dvr->m360Hardware[0]) {
        dvr->m360Hardware[0]->disableMsgType(CAMERA_MSG_VIDEO_FRAME);
        dvr->m360Hardware[0]->stopRecording();
    }
    if (dvr->mRecordCamera) {
        dvr->mRecordCamera->storage_state = 0;
        dvr->mRecordCamera->stopRecord();
        dvr->mRecordCamera->dropQueue();
    }
}

// 退出/换 factory 时释放录像管线（勿 dvr_factory::stopRecord，含 2s sleep 且热拔易挂）。
void releaseDvrCapturePipeline(dvr_factory *dvr)
{
    stopDvrStoragePipeline(dvr);
}

void softStopDvrChannel(dvr_factory *dvr, bool previewOn, bool /*recordOn*/)
{
    if (!dvr) {
        return;
    }
    if (previewOn) {
        dvr->stopPriview();
    }
    // 拨卡后 recordOn 可能已为 false，但录像管线仍可能占用 VIDEO_FRAME，必须释放。
    releaseDvrCapturePipeline(dvr);
}

bool recordingRequested()
{
    if (qEnvironmentVariableIsSet("CARDESK_AHD_RECORD")) {
        return true;
    }
    return AhdSettings::instance().recordingEnabled() && g_recordStorageAvailable;
}

// 显示管线：开 VIDEO_FRAME 出图，但暂停写盘线程（勿 dvr->startRecord）。
void startPreviewVideoPipeline(dvr_factory *dvr, int cameraId)
{
    if (!dvr) {
        return;
    }
    int ret = -1;
    if (dvr->mHardwareCameras) {
        dvr->mHardwareCameras->enableMsgType(CAMERA_MSG_VIDEO_FRAME);
        ret = dvr->mHardwareCameras->startRecording();
    } else if (dvr->m360Hardware && dvr->m360Hardware[0]) {
        dvr->m360Hardware[0]->enableMsgType(CAMERA_MSG_VIDEO_FRAME);
        ret = dvr->m360Hardware[0]->startRecording();
    }
    if (ret != 0) {
        qWarning() << "[Ahd] startPreviewVideoPipeline enable VIDEO_FRAME failed camera" << cameraId;
        return;
    }
    if (dvr->mRecordCamera) {
        dvr->mRecordCamera->storage_state = 0;
        dvr->mRecordCamera->stopRecord();
        dvr->mRecordCamera->dropQueue();
    }
    qDebug() << "[Ahd] startPreviewVideoPipeline ok (display only) camera" << cameraId;
}

bool isStorageRootUsable(const QString &root)
{
    const QFileInfo info(root);
    return info.exists() && info.isDir() && info.isWritable();
}

bool isDvrStorageWriting(dvr_factory *dvr)
{
    if (!dvr || !dvr->mRecordCamera) {
        return false;
    }
    return dvr->mRecordCamera->storage_state == 1
        && dvr->mRecordCamera->recordStat == RECORD_STATE_STARTED;
}

bool startStorageRecordingSafe(dvr_factory *dvr, int cameraId, bool previewActive)
{
    if (!dvr) {
        return false;
    }
    if (previewActive) {
        stopDvrStoragePipeline(dvr);
    }
    dvr->startRecord();
    if (!isDvrStorageWriting(dvr)) {
        qWarning() << "[Ahd] startRecord did not enter storage writing state camera" << cameraId;
        if (previewActive) {
            startPreviewVideoPipeline(dvr, cameraId);
        }
        return false;
    }
    // 预览已开启时勿再调 startPreviewVideoPipeline：它会 stopRecord()，刚启动的写盘会被立刻关掉。
    return true;
}

void applyRecordDirToSdk(int cameraId, const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return;
    }
    QByteArray path = rootPath.toUtf8();
    config_set_curfiledir(cameraId, path.data());
    if (cameraId == 360) {
        config_set_curfiledir(0, path.data());
    }
}

bool isMultiCamFactory(int cameraId)
{
    return cameraId == 360 || cameraId == 180 || cameraId == 270 || cameraId == 271
        || cameraId == 272;
}

CameraHardware *recordingWatermarkHardware(dvr_factory *dvr)
{
    if (!dvr) {
        return nullptr;
    }
    if (isMultiCamFactory(dvr->mCameraId)) {
        return (dvr->m360Hardware && dvr->m360Hardware[0]) ? dvr->m360Hardware[0] : nullptr;
    }
    return dvr->mHardwareCameras;
}

void disableSdkWatermark(dvr_factory *dvr)
{
    if (!dvr) {
        return;
    }
    // 通道名由 Qt 绘制；关闭 SDK 预览水印，避免 NV21 里残留斜角 OSD 与 Qt 叠影。
    dvr->disableWaterMark();
    if (CameraHardware *hw = recordingWatermarkHardware(dvr)) {
        if (hw != dvr->mHardwareCameras) {
            hw->sendCommand(CAMERA_CMD_STOP_WATER_MARK, 0, 0);
        }
    }
}

void applyRecordingTimestampWatermark(dvr_factory *dvr)
{
    CameraHardware *hw = recordingWatermarkHardware(dvr);
    if (!hw) {
        return;
    }
    if (hw->sendCommand(CAMERA_CMD_START_WATER_MARK, 0, 0) != NO_ERROR) {
        qWarning() << "[Ahd] enable recording watermark failed camera" << dvr->mCameraId;
        return;
    }
    usleep(50000);
    // 仅一行：SDK 在录像帧上自动刷新时间；VIDEO_ONLY 不画到预览帧。
    hw->setWaterMarkMultiple(const_cast<char *>("64,64,0"), WATER_MARK_DISP_MODE_VIDEO_ONLY);
}

} // namespace

bool AhdCameraPool::uses360QuadrantCrop(int width, int height)
{
    return width >= 960 && height >= 480;
}

bool AhdCameraPool::uses360QuadrantCrop() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_uses360Compose && uses360QuadrantCrop(m_frames[0].width, m_frames[0].height);
}

bool AhdCameraPool::ensureDvrManagerInit()
{
    if (g_dvrManagerInited) {
        return true;
    }
    DvrRecordManagerInit();
    g_dvrManagerInited = true;
    return true;
}

void AhdCameraPool::globalInit()
{
    prepareSdkRuntimeOnce();
    detectUncleanShutdown();
    ensureDvrManagerInit();
}

void AhdCameraPool::globalCleanup()
{
    if (s_activePool) {
        s_activePool->stopAll();
    }
    clearAhdSessionMarker();
    g_uncleanHardwareRecovery = false;
    g_dvrManagerInited = false;
    g_displayInited = false;
}

AhdCameraPool::AhdCameraPool(QObject *parent)
    : QObject(parent)
{
    g_recordStorageAvailable = AhdRecordStore::hasRecordStorage();
    connect(AppSignals::instance(), &AppSignals::sdcardStateChanged, this, [this](bool ready) {
        g_recordStorageAvailable = ready;
        if (!m_running) {
            return;
        }
        if (ready) {
            armDeferredRecording();
            scheduleRecordingSync();
            return;
        }
        m_pendingRecordingStart = false;
        if (m_recordingDeferTimer) {
            m_recordingDeferTimer->stop();
        }
        if (m_recordSyncScheduleTimer && m_recordSyncScheduleTimer->isActive()) {
            m_recordSyncScheduleTimer->stop();
        }
        syncRecordingState();
    });
}

AhdCameraPool::~AhdCameraPool()
{
    stopAll();
    if (s_activePool == this) {
        s_activePool = nullptr;
    }
}

bool AhdCameraPool::canResumeFactories(const QVector<int> &cameraIds) const
{
    for (int n = 0; n < cameraIds.size(); ++n) {
        const int cameraId = cameraIds.at(n);
        const int slot = poolSlotForCameraId(cameraId);
        if (slot < 0 || slot >= kChannelCount) {
            return false;
        }
        auto *dvr = static_cast<dvr_factory *>(m_channels[slot].dvr);
        if (!dvr || m_channels[slot].cameraId != cameraId || !isFactoryReady(dvr, cameraId)) {
            return false;
        }
    }
    return !cameraIds.isEmpty();
}

bool AhdCameraPool::resumePreview(const QVector<int> &cameraIds, bool hideHwOverlayImmediately)
{
    struct view_info vv = {0, 0, 1280, 720};

    for (int n = 0; n < cameraIds.size(); ++n) {
        const int cameraId = cameraIds.at(n);
        const int slot = poolSlotForCameraId(cameraId);
        ChannelState &ch = m_channels[slot];
        auto *dvr = static_cast<dvr_factory *>(ch.dvr);
        if (!dvr) {
            return false;
        }

        qDebug() << "[Ahd] resume camera" << cameraId << dvr;
        if (!ch.recordInited) {
            if (dvr->recordInit() != 0) {
                qWarning() << "[Ahd] resume recordInit failed camera" << cameraId;
            } else {
                ch.recordInited = true;
            }
        }
        if (cameraId != 360 && dvr->start() < 0) {
            return false;
        }
        startPreviewVideoPipeline(dvr, cameraId);
        if (dvr->startPriview(vv) != 0) {
            return false;
        }
        ch.previewOn = true;
        disableSdkWatermark(dvr);
        if (hideHwOverlayImmediately) {
            applyHideHwOverlayOnly(cameraId);
        }
    }

    if (!cameraIds.isEmpty()) {
        if (hideHwOverlayImmediately) {
            applyHideAllHwOverlays();
            QTimer::singleShot(0, this, [this]() {
                if (!m_running || m_shuttingDown) {
                    return;
                }
                applyHideAllHwOverlays();
            });
        } else {
            scheduleHideHwOverlay(cameraIds.first());
        }
    }
    applySafetyWatermarks(QString());
    return true;
}

void AhdCameraPool::applyHideHwOverlayOnly(int cameraId)
{
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_channels[i].cameraId != cameraId || !m_channels[i].dvr) {
            continue;
        }
        auto *dvr = static_cast<dvr_factory *>(m_channels[i].dvr);
        if (cameraId == 360 && dvr->m360Hardware) {
            for (int h = 0; h < NUM_OF_360CAMERAS; ++h) {
                if (dvr->m360Hardware[h]) {
                    dvr->m360Hardware[h]->mPreviewWindow.setPreviewDisplayOff();
                }
            }
            qDebug() << "[Ahd] hide HW overlay on 360 (4 paths), callbacks left to SDK";
        } else if (dvr->mHardwareCameras) {
            dvr->mHardwareCameras->mPreviewWindow.setPreviewDisplayOff();
        }
        break;
    }
}

void AhdCameraPool::applyHideAllHwOverlays()
{
#ifdef CAR_DESK_USE_T507_SDK
    for (int i = 0; i < kChannelCount; ++i) {
        if (!m_channels[i].dvr) {
            continue;
        }
        applyHideHwOverlayOnly(m_channels[i].cameraId);
    }
#endif
}

void AhdCameraPool::scheduleHideHwOverlay(int cameraId)
{
    QTimer::singleShot(kHwOverlayHideDelayMs, this, [this, cameraId]() {
        if (!m_running || m_shuttingDown) {
            return;
        }
        applyHideHwOverlayOnly(cameraId);
    });
}

bool AhdCameraPool::startAll(bool hideHwOverlayImmediately)
{
    if (m_running) {
        if (hideHwOverlayImmediately) {
            applyHideAllHwOverlays();
        }
        return true;
    }

    QString otherInstanceDetail;
    if (ProcessGuard::hasOtherCarDeskInstances(&otherInstanceDetail)) {
        qCritical().noquote() << otherInstanceDetail;
        emit poolError(QStringLiteral("Other CarDesk instance holds the camera; reboot the device and retry"));
        return false;
    }

    qDebug() << "[Ahd] startAll: layout mode" << m_layoutSpec.mode
             << "hideHwOverlayImmediately=" << hideHwOverlayImmediately;
    prepareSdkRuntimeOnce();
    performHardwareRecovery("before open");

    if (!ensureDvrManagerInit()) {
        emit poolError(QStringLiteral("DVR 管理器初始化失败"));
        return false;
    }

    const QVector<int> cameraIds = cameraIdsForLayout(m_layoutSpec);
    if (cameraIds.isEmpty()) {
        emit poolError(QStringLiteral("未配置摄像头"));
        return false;
    }

    m_uses360Compose = cameraIds.contains(360);

    if (canResumeFactories(cameraIds)) {
        m_shuttingDown = false;
        s_activePool = this;
        if (resumePreview(cameraIds, hideHwOverlayImmediately)) {
            m_running = true;
            resetPreviewFpsStats();
            applySafetyWatermarks(QString());
            armDeferredRecording();
            markAhdSessionActive();
            noteHardwareRecoveryDone();
            qDebug() << "[Ahd] startAll resumed (no delete, avoid SDK ~dvr_factory crash)";
            return true;
        }
        if (s_activePool == this) {
            s_activePool = nullptr;
        }
        qWarning() << "[Ahd] resume failed, need new dvr_factory (orphan old, no delete)";
    }

    m_activeChannelCount = 0;

    struct OpenEntry {
        int cameraId = 0;
        int slot = 0;
        dvr_factory *dvr = nullptr;
    };
    QVector<OpenEntry> opened;

    for (int n = 0; n < cameraIds.size(); ++n) {
        const int cameraId = cameraIds.at(n);
        const int slot = poolSlotForCameraId(cameraId);
        if (slot < 0 || slot >= kChannelCount) {
            continue;
        }

        ChannelState &ch = m_channels[slot];

        if (ch.dvr) {
            qWarning() << "[Ahd] orphan old dvr_factory slot" << slot << "(intentional leak, no delete)";
            auto *old = static_cast<dvr_factory *>(ch.dvr);
            softStopDvrChannel(old, ch.previewOn, ch.recordOn);
            ch = ChannelState();
        }

        dvr_factory *dvr = nullptr;
        for (int attempt = 0; attempt < kFactoryCreateRetries; ++attempt) {
            qDebug() << "[Ahd] phase1 new dvr_factory(" << cameraId << ") slot" << slot
                     << "attempt" << (attempt + 1);
            dvr = new (std::nothrow) dvr_factory(cameraId);
            if (dvr && isFactoryReady(dvr, cameraId)) {
                qDebug() << "[Ahd] phase1 dvr_factory(" << cameraId << ") ok" << dvr;
                break;
            }
            qWarning() << "[Ahd] dvr_factory(" << cameraId << ") init incomplete, retry (leak partial)";
            dvr = nullptr;
            QThread::msleep(kFactoryRetryDelayMs);
        }

        if (!dvr) {
            emit poolError(QStringLiteral("创建 dvr_factory(%1) 失败（设备可能被占用）").arg(cameraId));
            stopAll();
            return false;
        }

        ch = ChannelState();
        ch.cameraId = cameraId;
        ch.dvr = dvr;

        dvr->SetDataCB(poolUsrDataCb, dvr);
        dvr->setCallbacks(poolNotifyCallback, poolDataCallback, poolDataCallbackTimestamp, dvr);

        OpenEntry entry;
        entry.cameraId = cameraId;
        entry.slot = slot;
        entry.dvr = dvr;
        opened.append(entry);
        if (slot + 1 > m_activeChannelCount) {
            m_activeChannelCount = slot + 1;
        }

        if (n + 1 < cameraIds.size()) {
            QThread::msleep(kChannelStartDelayMs);
        }
    }

    const QStringList recordRoots = AhdRecordStore::recordRootPaths();

    for (int n = 0; n < opened.size(); ++n) {
        const OpenEntry &e = opened.at(n);
        ChannelState &ch = m_channels[e.slot];
        if (!ch.recordInited) {
            if (!recordRoots.isEmpty()) {
                applyRecordDirToSdk(e.cameraId, recordRoots.first());
            }
            qDebug() << "[Ahd] phase2 recordInit camera" << e.cameraId;
            if (e.dvr->recordInit() != 0) {
                qWarning() << "[Ahd] recordInit failed camera" << e.cameraId;
            } else {
                ch.recordInited = true;
            }
        }
        if (n + 1 < opened.size()) {
            QThread::msleep(kChannelStartDelayMs);
        }
    }

    for (const OpenEntry &e : opened) {
        qDebug() << "[Ahd] phase3 start() camera" << e.cameraId;
        if (e.dvr->start() < 0) {
            emit poolError(QStringLiteral("摄像头 %1 start() 失败").arg(e.cameraId));
            stopAll();
            return false;
        }
    }

    if (!g_displayInited) {
        qDebug() << "[Ahd] DisplayInit camera" << opened.first().cameraId;
        DisplayInit(DISP_GET_CONFIG, opened.first().cameraId);
        g_displayInited = true;
    }

    struct view_info vv = {0, 0, 1280, 720};
    m_running = true;
    s_activePool = this;
    resetPreviewFpsStats();

    for (const OpenEntry &e : opened) {
        qDebug() << "[Ahd] phase4 preview pipeline camera" << e.cameraId;
        ChannelState &ch = m_channels[e.slot];
        startPreviewVideoPipeline(e.dvr, e.cameraId);
        qDebug() << "[Ahd] phase5 startPriview camera" << e.cameraId;
        if (e.dvr->startPriview(vv) != 0) {
            m_running = false;
            if (s_activePool == this) {
                s_activePool = nullptr;
            }
            emit poolError(QStringLiteral("摄像头 %1 预览失败").arg(e.cameraId));
            stopAll();
            return false;
        }
        ch.previewOn = true;
        disableSdkWatermark(e.dvr);
        if (hideHwOverlayImmediately) {
            applyHideHwOverlayOnly(e.cameraId);
        }
    }

    if (!opened.isEmpty()) {
        if (hideHwOverlayImmediately) {
            applyHideAllHwOverlays();
            QTimer::singleShot(0, this, [this]() {
                if (!m_running || m_shuttingDown) {
                    return;
                }
                applyHideAllHwOverlays();
            });
        } else {
            scheduleHideHwOverlay(opened.first().cameraId);
        }
    }

    applySafetyWatermarks(QString());
    armDeferredRecording();

    markAhdSessionActive();
    noteHardwareRecoveryDone();
    qDebug() << "[Ahd] startAll done, cameras:" << cameraIds << "compose360=" << m_uses360Compose;
    return true;
}

void AhdCameraPool::stopAll()
{
    bool needsStop = m_running;
    for (int i = 0; i < kChannelCount && !needsStop; ++i) {
        if (m_channels[i].dvr) {
            needsStop = true;
        }
    }
    if (!needsStop) {
        return;
    }

    m_pendingRecordingStart = false;
    if (m_recordingDeferTimer) {
        m_recordingDeferTimer->stop();
    }
    qDebug() << "[Ahd] stopAll (stopPriview only, keep dvr_factory — never delete)";
    m_shuttingDown = true;
    const bool wasRecording = isRecordingActive();
    m_running = false;
    if (s_activePool == this) {
        s_activePool = nullptr;
    }
    resetPreviewFpsStats();
    emit previewFpsChanged();

    if (QCoreApplication::instance()) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 80);
    }

    for (int i = 0; i < kChannelCount; ++i) {
        ChannelState &ch = m_channels[i];
        if (!ch.dvr) {
            continue;
        }
        auto *dvr = static_cast<dvr_factory *>(ch.dvr);
        softStopDvrChannel(dvr, ch.previewOn, ch.recordOn);
        ch.previewOn = false;
        ch.recordOn = false;
    }

    if (wasRecording) {
        setRecordingFaultMonitorActive(false);
        emit recordingActiveChanged(false);
    }

    {
        QMutexLocker lock(&m_frameMutex);
        for (int i = 0; i < kChannelCount; ++i) {
            m_frames[i] = FrameSlot();
        }
    }
    m_qtPreviewDeliveryEnabled = false;
    for (int i = 0; i < kChannelCount; ++i) {
        m_previewStreamAlive[i] = false;
    }

    m_shuttingDown = false;
}

bool AhdCameraPool::isRunning() const
{
    return m_running;
}

void AhdCameraPool::setLayoutSpec(const AhdLayoutSpec &spec)
{
    m_layoutSpec = spec;
}

AhdLayoutSpec AhdCameraPool::layoutSpec() const
{
    return m_layoutSpec;
}

void AhdCameraPool::setQtPreviewDeliveryEnabled(bool enabled)
{
    if (m_qtPreviewDeliveryEnabled == enabled) {
        return;
    }
    m_qtPreviewDeliveryEnabled = enabled;
    // 退出预览时不要清空 m_frames：保留最后一帧，使再次进入能立即显示上一帧画面，
    // 避免等待首帧到来期间出现黑屏闪烁。新帧到来后会自动覆盖。
}

bool AhdCameraPool::isQtPreviewDeliveryEnabled() const
{
    return m_qtPreviewDeliveryEnabled;
}

void AhdCameraPool::onPreviewFrameFromSdk(void *dvrUser, char *dataPtr)
{
    if (m_shuttingDown || !m_running || !dvrUser || !dataPtr) {
        return;
    }

    auto *buf = reinterpret_cast<V4L2BUF_t *>(dataPtr);
    const int width = static_cast<int>(buf->width);
    const int height = static_cast<int>(buf->height);
    if (width <= 0 || height <= 0 || width > kMaxFrameWidth || height > kMaxFrameHeight
        || !buf->addrVirY) {
        return;
    }

    int channelIndex = -1;
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_channels[i].dvr == dvrUser) {
            channelIndex = poolSlotForCameraId(m_channels[i].cameraId);
            break;
        }
    }
    if (channelIndex < 0) {
        return;
    }

    m_previewStreamAlive[channelIndex] = true;
    if (!m_qtPreviewDeliveryEnabled) {
        if (isRecordingActive()) {
            recordFrameForFaultCheck(channelIndex);
        }
        tryStartRecordingWhenReady();
        return;
    }

    const int byteSize = width * height * 3 / 2;
    QByteArray copy(byteSize, Qt::Uninitialized);
    std::memcpy(copy.data(), reinterpret_cast<void *>(buf->addrVirY),
                static_cast<size_t>(byteSize));

    const qint64 timestampUs = static_cast<qint64>(buf->timeStamp);

    QMetaObject::invokeMethod(this, "deliverPreviewFrame", Qt::QueuedConnection,
                              Q_ARG(int, channelIndex), Q_ARG(QByteArray, copy), Q_ARG(int, width),
                              Q_ARG(int, height), Q_ARG(qint64, timestampUs));
}

void AhdCameraPool::deliverPreviewFrame(int channelIndex, QByteArray nv21, int width, int height,
                                        qint64 timestampUs)
{
    if (m_shuttingDown || !m_running || channelIndex < 0 || channelIndex >= kChannelCount
        || nv21.isEmpty()) {
        return;
    }

    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
    if (static_cast<size_t>(nv21.size()) < bytes) {
        return;
    }

    {
        QMutexLocker lock(&m_frameMutex);
        m_frames[channelIndex].nv21 = std::move(nv21);
        m_frames[channelIndex].width = width;
        m_frames[channelIndex].height = height;
        m_frames[channelIndex].timestampUs = timestampUs;
        ++m_frames[channelIndex].generation;
    }

    if (isRecordingActive()) {
        recordFrameForFaultCheck(channelIndex);
    }

    notePreviewFpsSample(channelIndex);
    emit framesUpdated();
    tryStartRecordingWhenReady();
}

QString AhdCameraPool::faultTextForType(CamFaultType type)
{
    switch (type) {
    case CamFaultType::StreamInterrupt:
        return QStringLiteral("摄像头故障");
    case CamFaultType::LowFps:
        return QStringLiteral("请检查摄像头");
    default:
        return QString();
    }
}

QString AhdCameraPool::cameraFaultText(int channelIndex) const
{
    if (!isRecordingActive() || channelIndex < 0 || channelIndex >= kChannelCount) {
        return QString();
    }
    return faultTextForType(m_channelFaults[channelIndex].fault);
}

void AhdCameraPool::resetPreviewFpsStats()
{
    for (int i = 0; i < kChannelCount; ++i) {
        m_previewFpsCount[i] = 0;
        m_channelPreviewFps[i] = 0.0;
    }
    m_previewFpsWindowStartMs = 0;
}

void AhdCameraPool::notePreviewFpsSample(int channelIndex)
{
    if (!AppSettings::debugMode() || channelIndex < 0 || channelIndex >= kChannelCount) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_previewFpsCount[channelIndex]++;
    if (m_previewFpsWindowStartMs == 0) {
        m_previewFpsWindowStartMs = now;
        return;
    }

    const qint64 elapsed = now - m_previewFpsWindowStartMs;
    if (elapsed < 1000) {
        return;
    }

    for (int i = 0; i < kChannelCount; ++i) {
        m_channelPreviewFps[i] = m_previewFpsCount[i] * 1000.0 / static_cast<double>(elapsed);
        m_previewFpsCount[i] = 0;
    }
    m_previewFpsWindowStartMs = now;
    emit previewFpsChanged();
}

double AhdCameraPool::channelPreviewFps(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= kChannelCount) {
        return 0.0;
    }
    return m_channelPreviewFps[channelIndex];
}

void AhdCameraPool::resetFaultState()
{
    for (int i = 0; i < kChannelCount; ++i) {
        m_channelFaults[i] = ChannelFaultState();
    }
    m_recordingFaultMonitorSinceMs = 0;
}

void AhdCameraPool::setRecordingFaultMonitorActive(bool active)
{
    if (active) {
        resetFaultState();
        m_recordingFaultMonitorSinceMs = QDateTime::currentMSecsSinceEpoch();
        if (!m_faultCheckTimer) {
            m_faultCheckTimer = new QTimer(this);
            m_faultCheckTimer->setInterval(500);
            connect(m_faultCheckTimer, &QTimer::timeout, this, &AhdCameraPool::updateChannelFaults);
        }
        m_faultCheckTimer->start();
    } else {
        if (m_faultCheckTimer) {
            m_faultCheckTimer->stop();
        }
        resetFaultState();
        emit cameraFaultsChanged();
    }
}

bool AhdCameraPool::evaluateChannelFault(ChannelFaultState &st, qint64 now, qint64 sinceRec)
{
    const CamFaultType before = st.fault;

    if (!st.hasFrame) {
        if (sinceRec > CAM_FAULT_TIMEOUT_MS) {
            st.fault = CamFaultType::StreamInterrupt;
        }
        return st.fault != before;
    }

    if (st.fault == CamFaultType::StreamInterrupt) {
        return false;
    }

    const qint64 elapsed = now - st.lastFrameWallMs;
    if (elapsed > CAM_FAULT_TIMEOUT_MS) {
        st.fault = CamFaultType::StreamInterrupt;
        return st.fault != before;
    }

    if (st.fpsWindowStartMs == 0) {
        st.fpsWindowStartMs = now;
        st.fpsFrameCount = qMax(st.fpsFrameCount, 1);
        return false;
    }

    const qint64 windowElapsed = now - st.fpsWindowStartMs;
    if (windowElapsed < CAM_FPS_WINDOW_MS) {
        return false;
    }

    if (st.fpsFrameCount < CAM_FPS_MIN_FRAMES) {
        st.fault = CamFaultType::LowFps;
    } else if (st.fault == CamFaultType::LowFps) {
        st.fault = CamFaultType::None;
    }

    st.fpsWindowStartMs = now;
    st.fpsFrameCount = 0;
    return st.fault != before;
}

void AhdCameraPool::recordFrameForFaultCheck(int camId)
{
    if (camId < 0 || camId >= kChannelCount || !isRecordingActive()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ChannelFaultState &st = m_channelFaults[camId];
    st.lastFrameWallMs = now;
    st.hasFrame = true;
    st.fpsFrameCount++;

    // 断流恢复：仅清除断流故障，低帧率由完整统计窗口判定（对齐 MultiCameraCompose）
    if (st.fault == CamFaultType::StreamInterrupt) {
        st.fault = CamFaultType::None;
        emit cameraFaultsChanged();
    }
}

void AhdCameraPool::updateChannelFaults()
{
    if (!isRecordingActive()) {
        return;
    }

    bool changed = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceRec =
        m_recordingFaultMonitorSinceMs > 0 ? (now - m_recordingFaultMonitorSinceMs) : 0;

    for (int i = 0; i < kChannelCount; ++i) {
        if (evaluateChannelFault(m_channelFaults[i], now, sinceRec)) {
            changed = true;
        }
    }

    if (changed) {
        emit cameraFaultsChanged();
    }
}

void AhdCameraPool::armDeferredRecording()
{
    m_pendingRecordingStart = recordingRequested();
    if (!m_pendingRecordingStart) {
        return;
    }

    if (!m_recordingDeferTimer) {
        m_recordingDeferTimer = new QTimer(this);
        m_recordingDeferTimer->setSingleShot(true);
        connect(m_recordingDeferTimer, &QTimer::timeout, this, [this]() {
            if (!m_pendingRecordingStart || !m_running || m_shuttingDown) {
                return;
            }
            qWarning() << "[Ahd] deferred recording fallback (preview frame timeout)";
            m_pendingRecordingStart = false;
            QMetaObject::invokeMethod(this, "scheduleRecordingSync", Qt::QueuedConnection);
        });
    }
    // 插 TF 时勿在 startAll 同步 recordInit；若迟迟无预览帧则超时兜底启动录像。
    m_recordingDeferTimer->start(8000);
    qDebug() << "[Ahd] recording deferred until preview frames ready";
}

void AhdCameraPool::tryStartRecordingWhenReady()
{
    if (!m_pendingRecordingStart || m_shuttingDown || !m_running || !recordingRequested()) {
        return;
    }

    bool hasFrame = false;
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_previewStreamAlive[i]) {
            hasFrame = true;
            break;
        }
    }
    if (!hasFrame) {
        QMutexLocker lock(&m_frameMutex);
        for (int i = 0; i < kChannelCount; ++i) {
            if (m_frames[i].generation != 0 && !m_frames[i].nv21.isEmpty()) {
                hasFrame = true;
                break;
            }
        }
    }
    if (!hasFrame) {
        return;
    }

    m_pendingRecordingStart = false;
    if (m_recordingDeferTimer) {
        m_recordingDeferTimer->stop();
    }
    qDebug() << "[Ahd] preview frames ready, starting deferred recording";
    QMetaObject::invokeMethod(this, "scheduleRecordingSync", Qt::QueuedConnection);
}

void AhdCameraPool::scheduleRecordingSync()
{
    if (m_shuttingDown || !m_running) {
        return;
    }
    if (!m_recordSyncScheduleTimer) {
        m_recordSyncScheduleTimer = new QTimer(this);
        m_recordSyncScheduleTimer->setSingleShot(true);
        m_recordSyncScheduleTimer->setInterval(1500);
        connect(m_recordSyncScheduleTimer, &QTimer::timeout, this, &AhdCameraPool::syncRecordingState);
    }
    m_recordSyncScheduleTimer->start();
}

void AhdCameraPool::syncRecordingState()
{
    if (m_shuttingDown || !m_running) {
        return;
    }
    if (m_recordSyncBusy) {
        m_recordSyncPending = true;
        return;
    }
    m_recordSyncBusy = true;

    struct StorageStopEntry {
        dvr_factory *dvr = nullptr;
        int cameraId = 0;
        ChannelState *ch = nullptr;
    };

    const bool wasActive = isRecordingActive();
    const bool want = recordingRequested();
    QVector<StorageStopEntry> storageStops;

    {
        QMutexLocker lock(&m_recordSyncMutex);

        for (int i = 0; i < kChannelCount; ++i) {
            ChannelState &ch = m_channels[i];
            if (!ch.dvr) {
                continue;
            }
            auto *dvr = static_cast<dvr_factory *>(ch.dvr);

            if (want && !ch.recordOn) {
                const QStringList roots = AhdRecordStore::recordRootPaths();
                if (roots.isEmpty()) {
                    continue;
                }
                const QString root = roots.first();
                if (!isStorageRootUsable(root)) {
                    qWarning() << "[Ahd] storage path not ready:" << root;
                    continue;
                }
                applyRecordDirToSdk(ch.cameraId, root);
                if (!ch.recordInited) {
                    if (dvr->recordInit() != 0) {
                        qWarning() << "[Ahd] recordInit failed camera" << ch.cameraId;
                        continue;
                    }
                    ch.recordInited = true;
                }
                if (dvr->mRecordCamera) {
                    dvr->mRecordCamera->setDuration(60);
                }
                if (!startStorageRecordingSafe(dvr, ch.cameraId, ch.previewOn)) {
                    qWarning() << "[Ahd] start storage recording failed camera" << ch.cameraId;
                    continue;
                }
                ch.recordOn = true;
                applyRecordingTimestampWatermark(dvr);
                qDebug() << "[Ahd] storage recording started camera" << ch.cameraId;
            } else if (!want && (ch.recordOn || isDvrStorageWriting(dvr))) {
                ch.recordOn = false;
                StorageStopEntry stopEntry;
                stopEntry.dvr = dvr;
                stopEntry.cameraId = ch.cameraId;
                stopEntry.ch = &ch;
                storageStops.append(stopEntry);
            }
        }
    }

    if (!storageStops.isEmpty()) {
        qDebug() << "[Ahd] stopping storage pipeline on" << storageStops.size() << "channel(s)";
        for (const StorageStopEntry &entry : storageStops) {
            if (!entry.dvr) {
                continue;
            }
            stopDvrStoragePipeline(entry.dvr);
            disableSdkWatermark(entry.dvr);
            startPreviewVideoPipeline(entry.dvr, entry.cameraId);
            qDebug() << "[Ahd] storage stopped (display kept) camera" << entry.cameraId;
        }
    }

    const bool nowActive = isRecordingActive();
    if (wasActive != nowActive) {
        setRecordingFaultMonitorActive(nowActive);
        emit recordingActiveChanged(nowActive);
    }

    m_recordSyncBusy = false;
    if (m_recordSyncPending) {
        m_recordSyncPending = false;
        QMetaObject::invokeMethod(this, "scheduleRecordingSync", Qt::QueuedConnection);
    }
}

bool AhdCameraPool::isRecordingActive() const
{
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_channels[i].recordOn) {
            return true;
        }
    }
    return false;
}

void AhdCameraPool::applySafetyWatermarks(const QString &text)
{
    // 水印状态完全由录像开始/停止（syncRecordingState）驱动，且 SDK 后台常驻、
    // 水印会一直保持。进入/恢复预览时不要在此（主线程）重设或阻塞水印——
    // applyRecordingTimestampWatermark 内含 usleep，曾导致再次进入时主线程被阻塞、
    // 预览窗口来不及绘制而黑屏闪烁。这里保持空操作。
    Q_UNUSED(text);
}

bool AhdCameraPool::copyLatestFrame(int channelIndex, FrameSlot *out) const
{
    if (!out || channelIndex < 0 || channelIndex >= kChannelCount) {
        return false;
    }

    QMutexLocker lock(&m_frameMutex);
    if (m_frames[channelIndex].generation == 0 || m_frames[channelIndex].nv21.isEmpty()) {
        return false;
    }
    *out = m_frames[channelIndex];
    return true;
}

#else // CAR_DESK_USE_T507_SDK

bool AhdCameraPool::uses360QuadrantCrop(int, int) { return false; }

bool AhdCameraPool::uses360QuadrantCrop() const { return false; }

void AhdCameraPool::globalInit() {}
void AhdCameraPool::globalCleanup() {}

AhdCameraPool::AhdCameraPool(QObject *parent)
    : QObject(parent)
{
}

AhdCameraPool::~AhdCameraPool() {}

bool AhdCameraPool::startAll(bool hideHwOverlayImmediately)
{
    Q_UNUSED(hideHwOverlayImmediately);
    m_running = true;
    return true;
}

void AhdCameraPool::stopAll()
{
    m_running = false;
}

bool AhdCameraPool::isRunning() const { return m_running; }

void AhdCameraPool::setLayoutSpec(const AhdLayoutSpec &spec) { m_layoutSpec = spec; }

AhdLayoutSpec AhdCameraPool::layoutSpec() const { return m_layoutSpec; }

bool AhdCameraPool::copyLatestFrame(int channelIndex, FrameSlot *out) const
{
    Q_UNUSED(channelIndex);
    Q_UNUSED(out);
    return false;
}

void AhdCameraPool::setQtPreviewDeliveryEnabled(bool enabled)
{
    m_qtPreviewDeliveryEnabled = enabled;
}

bool AhdCameraPool::isQtPreviewDeliveryEnabled() const
{
    return m_qtPreviewDeliveryEnabled;
}

void AhdCameraPool::applySafetyWatermarks(const QString &text)
{
    Q_UNUSED(text);
}

void AhdCameraPool::syncRecordingState() {}

void AhdCameraPool::scheduleRecordingSync() {}

bool AhdCameraPool::isRecordingActive() const
{
    return false;
}

QString AhdCameraPool::cameraFaultText(int channelIndex) const
{
    Q_UNUSED(channelIndex);
    return QString();
}

double AhdCameraPool::channelPreviewFps(int channelIndex) const
{
    Q_UNUSED(channelIndex);
    return 0.0;
}

#endif // CAR_DESK_USE_T507_SDK
