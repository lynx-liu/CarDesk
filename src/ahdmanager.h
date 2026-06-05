#ifndef AHDMANAGER_H
#define AHDMANAGER_H

#include <QObject>
#include <QRect>
#include <QString>

class QWidget;

#include "ahdlayout.h"

class AhdCameraPool;
class AhdPreviewGLWidget;
class AhdPreviewOverlayWidget;

class QTimer;

// 进程内 dvr_factory + Qt 窗口预览（对齐 sdktest 初始化顺序）
class AhdManager : public QObject {
    Q_OBJECT
public:
    static void globalInit();
    static void globalCleanup();

    explicit AhdManager(int layoutMode = 360, QObject *parent = nullptr);
    ~AhdManager() override;

    void setLayoutMode(int mode);
    int layoutMode() const;

    void setPreviewCameraIndex(int previewCameraIndex);

    bool startCamera();
    bool warmupHardware();
    void stopCamera();

    bool startPreview(QWidget *parentWidget, int x, int y, int w, int h);
    void stopPreview();

    bool isCameraReady() const;
    bool isPreviewActive() const;
    bool hasWarmCameraPool() const;

    AhdPreviewGLWidget *previewWidget() const;

    void enableSafetyWatermark(const QString &text = QStringLiteral("请注意周边安全"));
    void clearWatermark();
    void syncRecordingWithSettings();
    void syncRecordingWithSettingsNow();

signals:
    void cameraError(const QString &msg);
    void previewStarted();
    void previewStopped();

private:
    void applyLayoutSpec();
    void attachPreviewWidget(QWidget *parentWidget, int w, int h);
    void updateRecordingBadge();
    void updateCameraFaultOverlay();
    void flushRecordingSync();

    AhdCameraPool *m_pool;
    AhdPreviewGLWidget *m_previewWidget;
    AhdPreviewOverlayWidget *m_overlayWidget = nullptr;
    QTimer *m_recordSyncTimer = nullptr;
    AhdLayoutSpec m_layout;
    bool m_camReady;
    bool m_prevActive;
    int m_previewCameraIndex;
    QRect m_lastRect;
};

#endif // AHDMANAGER_H
