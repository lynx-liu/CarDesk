#ifndef AHDCAMERAPOOL_H
#define AHDCAMERAPOOL_H

#include <QByteArray>
#include <QMutex>
#include <QObject>

#include "ahdlayout.h"

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
    bool hasPersistedFactories() const;
    bool isShuttingDown() const { return m_shuttingDown; }
    bool uses360Compose() const { return m_uses360Compose; }
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

    void applySafetyWatermarks(const QString &text);
    void syncRecordingState();

#ifdef CAR_DESK_USE_T507_SDK
    void onPreviewFrameFromSdk(void *dvrUser, char *dataPtr);
    static AhdCameraPool *s_activePool;
#endif

signals:
    void framesUpdated();
    void poolError(const QString &message);

private slots:
#ifdef CAR_DESK_USE_T507_SDK
    void deliverPreviewFrame(int channelIndex, QByteArray nv21, int width, int height, qint64 timestampUs);
#endif

private:
#ifdef CAR_DESK_USE_T507_SDK
    static bool ensureDvrManagerInit();
    bool canResumeFactories(const QVector<int> &cameraIds) const;
    bool resumePreview(const QVector<int> &cameraIds, bool hideHwOverlayImmediately);
    void scheduleHideHwOverlay(int cameraId);
    void applyHideHwOverlayOnly(int cameraId);
    void applyHideAllHwOverlays();

    struct ChannelState {
        int cameraId = 0;
        void *dvr = nullptr; // android::dvr_factory*
        bool previewOn = false;
        bool recordOn = false;
    };
    ChannelState m_channels[kChannelCount];
    int m_activeChannelCount = 0;
    bool m_uses360Compose = false;
#endif

    mutable QMutex m_frameMutex;
    FrameSlot m_frames[kChannelCount];
    AhdLayoutSpec m_layoutSpec;
    bool m_running = false;
    bool m_shuttingDown = false;
};

#endif // AHDCAMERAPOOL_H
