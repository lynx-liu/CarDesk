#ifndef AHDCAMERAPOOL_H
#define AHDCAMERAPOOL_H

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QVector>

class QTimer;

#include "ahdlayout.h"

/* 故障检测超时阈值 (毫秒) - 连续无新帧超过此时间判定为断流故障 */
#define CAM_FAULT_TIMEOUT_MS 3000

/* 低帧率故障检测阈值
 * 在FPS_WINDOW_MS毫秒窗口内，帧数低于FPS_MIN_FRAMES则判定为低帧率故障
 * 例如: 2000ms窗口内至少需要15帧(=7.5fps)，低于此值则判定异常
 */
#define CAM_FPS_WINDOW_MS     2000   /* 帧率统计时间窗口(ms) */
#define CAM_FPS_MIN_FRAMES    15     /* 窗口内最少帧数 (2000ms内15帧 = 7.5fps) */

class AhdCameraPool : public QObject {
    Q_OBJECT

public:
    static constexpr int kTvdDevIdStart = 2;
    static constexpr int kChannelCount = 4;
    static constexpr int kRecordChannelCount = 3;
    static constexpr int kMaxFrameWidth = 2560;
    static constexpr int kMaxFrameHeight = 1440;
    static bool uses360QuadrantCrop(int width, int height);

    explicit AhdCameraPool(QObject *parent = nullptr);
    ~AhdCameraPool() override;

    static void globalInit();
    static void globalCleanup();

    bool startAll(bool hideHwOverlayImmediately = false);
    void stopAll();

    bool isRunning() const;
    /** startAll 进行中（含 sleepYieldingUi），供上层避免重入导致黑屏 */
    bool isStartInProgress() const { return m_startInProgress; }
    bool hasPersistedFactories() const;
    bool isShuttingDown() const { return m_shuttingDown; }
#ifdef CAR_DESK_USE_T507_SDK
    bool uses360Compose() const { return m_uses360Compose; }
#else
    bool uses360Compose() const { return false; }
#endif
    bool uses360QuadrantCrop() const;

    void setLayoutSpec(const AhdLayoutSpec &spec);
    AhdLayoutSpec layoutSpec() const;

    struct FrameSlot {
        QByteArray nv21;
        int width = 0;
        int height = 0;
        qint64 timestampUs = 0;
        quint64 generation = 0;
    };

    bool copyLatestFrame(int channelIndex, FrameSlot *out) const;

    // 仅控制 Qt/OpenGL 预览帧拷贝；不影响 SDK 录像写盘。
    void setQtPreviewDeliveryEnabled(bool enabled);
    bool isQtPreviewDeliveryEnabled() const;

    void applySafetyWatermarks(const QString &text);
    bool isRecordingActive() const;

    // 录像期间摄像头故障文案（空串表示无故障）；channelIndex 0..3
    QString cameraFaultText(int channelIndex) const;

    // 预览帧率（约 1s 统计窗口）；无帧时为 0
    double channelPreviewFps(int channelIndex) const;

#ifdef CAR_DESK_USE_T507_SDK
    void onPreviewFrameFromSdk(void *dvrUser, char *dataPtr);
    static AhdCameraPool *s_activePool;
#endif

signals:
    void framesUpdated();
    void previewFpsChanged();
    void poolError(const QString &message);
    void recordingActiveChanged(bool active);
    void cameraFaultsChanged();

public slots:
    void syncRecordingState();
    void scheduleRecordingSync();

private slots:
#ifdef CAR_DESK_USE_T507_SDK
    void deliverPreviewFrame(int channelIndex, QByteArray nv21, int width, int height,
                             qint64 timestampUs);
#endif

private:
#ifdef CAR_DESK_USE_T507_SDK
    static bool ensureDvrManagerInit();
    bool canResumeFactories(const QVector<int> &cameraIds) const;
    bool resumePreview(const QVector<int> &cameraIds, bool hideHwOverlayImmediately);
    bool startAllImpl(bool hideHwOverlayImmediately);
    void scheduleHideHwOverlay(int cameraId);
    void applyHideHwOverlayOnly(int cameraId);
    void applyHideAllHwOverlays();

    struct ChannelState {
        int cameraId = 0;
        void *dvr = nullptr; // android::dvr_factory*
        bool previewOn = false;
        bool recordOn = false;
        bool recordInited = false;
    };
    ChannelState m_channels[kChannelCount];
    int m_activeChannelCount = 0;
    bool m_uses360Compose = false;
#endif

    mutable QMutex m_frameMutex;
    mutable QMutex m_recordSyncMutex;
    FrameSlot m_frames[kChannelCount];
    AhdLayoutSpec m_layoutSpec;
    bool m_running = false;
    bool m_startInProgress = false;
    bool m_shuttingDown = false;
    bool m_pendingRecordingStart = false;
    QTimer *m_recordingDeferTimer = nullptr;
    QTimer *m_recordSyncScheduleTimer = nullptr;
    bool m_recordSyncBusy = false;
    bool m_recordSyncPending = false;
    bool m_qtPreviewDeliveryEnabled = false;
    bool m_previewStreamAlive[kChannelCount] = {};

    void armDeferredRecording();
    void tryStartRecordingWhenReady();

    enum class CamFaultType {
        None,
        StreamInterrupt,
        LowFps,
    };

    struct ChannelFaultState {
        CamFaultType fault = CamFaultType::None;
        qint64 lastFrameWallMs = 0;
        qint64 fpsWindowStartMs = 0;
        int fpsFrameCount = 0;
        bool hasFrame = false;
    };

    ChannelFaultState m_channelFaults[kChannelCount];
    qint64 m_recordingFaultMonitorSinceMs = 0;
    QTimer *m_faultCheckTimer = nullptr;

    int m_previewFpsCount[kChannelCount] = {};
    double m_channelPreviewFps[kChannelCount] = {};
    qint64 m_previewFpsWindowStartMs = 0;

    void resetPreviewFpsStats();
    void notePreviewFpsSample(int channelIndex);
    void setRecordingFaultMonitorActive(bool active);
    void resetFaultState();
    void recordFrameForFaultCheck(int camId);
    void updateChannelFaults();
    bool evaluateChannelFault(ChannelFaultState &st, qint64 now, qint64 sinceRec);
    static QString faultTextForType(CamFaultType type);
};

inline bool AhdCameraPool::hasPersistedFactories() const
{
#ifdef CAR_DESK_USE_T507_SDK
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_channels[i].dvr) {
            return true;
        }
    }
#endif
    return false;
}

#endif // AHDCAMERAPOOL_H
