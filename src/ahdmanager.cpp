#include "ahdmanager.h"

#include <QProcess>
#include <QRect>
#include <QStringList>

// ============================================================
// T507 实现：改为外部 sdktest 进程托管
// 切换策略：每次模式变化时，先强杀旧 sdktest，再按新参数启动
// ============================================================
#ifdef CAR_DESK_USE_T507_SDK

namespace {

constexpr int kT507TvdDevIdStart = 2;
constexpr int kNum360Cameras = 4;

} // namespace

struct AhdManager::Private {
    QProcess   process;
    bool       camReady = false;
    bool       prevActive = false;
    int        previewCameraIndex = -1;
    int        lastAppliedPreviewCameraIndex = -2;
    QRect      lastPreviewRect;
    QStringList activeArgs;

    static QString programPath()
    {
        return QStringLiteral("sdktest");
    }

    static QStringList buildArgs(int cameraId, int previewCameraIndex)
    {
        // sdktest 参数格式：sdktest <camera_nums> <camera_id ...>
        // 文档示例：
        //   sdktest 1 360  -> 4分屏(/dev/video2-5)
        //   sdktest 1 2..5 -> 单路
        if (cameraId == 360) {
            if (previewCameraIndex >= 0 && previewCameraIndex < kNum360Cameras) {
                return {QStringLiteral("1"), QString::number(kT507TvdDevIdStart + previewCameraIndex)};
            }
            return {QStringLiteral("1"), QStringLiteral("360")};
        }

        return {QStringLiteral("1"), QString::number(cameraId)};
    }

    static void killAllSdktest()
    {
        // 用户要求：切换时直接杀进程。
        QProcess::execute(QStringLiteral("/bin/sh"),
                          {QStringLiteral("-c"), QStringLiteral("killall -9 sdktest >/dev/null 2>&1 || true")});
    }

    bool restartWithArgs(const QStringList &args, QString *error)
    {
        if (activeArgs == args && process.state() == QProcess::Running) {
            return true;
        }

        killAllSdktest();
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(500);
        }

        process.setProgram(programPath());
        process.setArguments(args);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start();

        if (!process.waitForStarted(1500)) {
            if (error) {
                *error = QStringLiteral("启动 sdktest 失败: %1 %2")
                             .arg(programPath(), args.join(QLatin1Char(' ')));
            }
            activeArgs.clear();
            return false;
        }

        activeArgs = args;
        return true;
    }

    void stopAll()
    {
        killAllSdktest();
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(500);
        }
        activeArgs.clear();
        prevActive = false;
        lastPreviewRect = QRect();
        lastAppliedPreviewCameraIndex = -2;
    }
};

void AhdManager::globalInit() {}

void AhdManager::globalCleanup()
{
    Private::killAllSdktest();
}

AhdManager::AhdManager(int cameraId, QObject *parent)
    : QObject(parent), d(new Private), m_cameraId(cameraId)
{}

AhdManager::~AhdManager()
{
    stopCamera();
    delete d;
}

void AhdManager::setCameraId(int cameraId)
{
    if (m_cameraId == cameraId) {
        return;
    }

    if (d->prevActive) {
        stopPreview();
    }
    if (d->camReady) {
        stopCamera();
    }

    m_cameraId = cameraId;
    d->previewCameraIndex = -1;
}

int AhdManager::cameraId() const { return m_cameraId; }

void AhdManager::setPreviewCameraIndex(int previewCameraIndex)
{
    if (previewCameraIndex < -1) {
        previewCameraIndex = -1;
    }
    if (previewCameraIndex >= kNum360Cameras) {
        previewCameraIndex = kNum360Cameras - 1;
    }
    d->previewCameraIndex = previewCameraIndex;
}

bool AhdManager::startCamera()
{
    d->camReady = true;
    return true;
}

void AhdManager::stopCamera()
{
    d->stopAll();
    d->camReady = false;
}

bool AhdManager::startPreview(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        emit cameraError(
            QStringLiteral("startPreview 参数无效 (cameraId=%1, rect=%2,%3 %4x%5)")
                .arg(m_cameraId).arg(x).arg(y).arg(w).arg(h));
        return false;
    }

    if (!d->camReady) {
        if (!startCamera()) {
            return false;
        }
    }

    const QRect requestedRect(x < 0 ? 0 : x,
                              y < 0 ? 0 : y,
                              w,
                              h);

    if (d->prevActive
        && d->lastPreviewRect == requestedRect
        && d->lastAppliedPreviewCameraIndex == d->previewCameraIndex) {
        return true;
    }

    const QStringList args = Private::buildArgs(m_cameraId, d->previewCameraIndex);
    QString error;
    if (!d->restartWithArgs(args, &error)) {
        emit cameraError(error);
        return false;
    }

    d->prevActive = true;
    d->lastPreviewRect = requestedRect;
    d->lastAppliedPreviewCameraIndex = d->previewCameraIndex;
    emit previewStarted();
    return true;
}

void AhdManager::stopPreview()
{
    if (!d->prevActive) {
        return;
    }

    d->stopAll();
    emit previewStopped();
}

bool AhdManager::isCameraReady() const { return d->camReady; }

bool AhdManager::isPreviewActive() const { return d->prevActive; }

void AhdManager::enableSafetyWatermark(const QString &text)
{
    Q_UNUSED(text);
}

void AhdManager::clearWatermark() {}

// ============================================================
// PC Stub 实现（无 T507 SDK 时生效）
// ============================================================
#else // CAR_DESK_USE_T507_SDK

struct AhdManager::Private {
    bool camReady   = false;
    bool prevActive = false;
};

void AhdManager::globalInit() {}
void AhdManager::globalCleanup() {}

AhdManager::AhdManager(int cameraId, QObject *parent)
    : QObject(parent), d(new Private), m_cameraId(cameraId)
{}

AhdManager::~AhdManager()
{
    delete d;
}

void AhdManager::setCameraId(int cameraId)
{
    m_cameraId = cameraId;
}

int AhdManager::cameraId() const { return m_cameraId; }

void AhdManager::setPreviewCameraIndex(int)
{}

bool AhdManager::startCamera()
{
    d->camReady = true;
    return true;
}

void AhdManager::stopCamera()
{
    if (d->prevActive) stopPreview();
    d->camReady = false;
}

bool AhdManager::startPreview(int x, int y, int w, int h)
{
    Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(w); Q_UNUSED(h);
    if (!d->camReady) startCamera();
    d->prevActive = true;
    emit previewStarted();
    return true;
}

void AhdManager::stopPreview()
{
    if (!d->prevActive) return;
    d->prevActive = false;
    emit previewStopped();
}

bool AhdManager::isCameraReady()   const { return d->camReady; }
bool AhdManager::isPreviewActive() const { return d->prevActive; }

void AhdManager::enableSafetyWatermark(const QString &) {}
void AhdManager::clearWatermark() {}

#endif // CAR_DESK_USE_T507_SDK
