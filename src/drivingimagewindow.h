#ifndef DRIVINGIMAGEWINDOW_H
#define DRIVINGIMAGEWINDOW_H

#include <QMainWindow>

#include "ahdmanager.h"

class QFrame;
class QHideEvent;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QTimer;
class DrivingImageNavBar;
class DrivingImagePreviewTopBar;
class DrivingImageSettingsPage;
class DrivingImagePlaybackPage;

class DrivingImageWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DrivingImageWindow(QWidget *parent = nullptr);
    void warmupCamera();
    void setDrivingMode(int mode);
    int drivingMode() const;

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void returnToMainSafely();
    void setupUI();
    void bindAhdSignals();
    void layoutCenterHint();
    void layoutTextOverlays();
    void layoutNavBar();
    void layoutLongPressHint();
    void layoutPreviewTopBar();
    void updatePreviewChrome();
    bool canStartPreviewLongPress(const QPoint &globalPos) const;
    void onLongPressTimeout();
    void handleConfirmedSingleClick(const QPoint &globalPos);
    void startPreviewIfNeeded();
    void updatePreviewLayout();
    void stopPreview();
    QRect previewRectOnScreen() const;
    void showPage(int index); // 0=预览 1=设置 2=回放
    QWidget *createPreviewPage();

    QStackedWidget *m_stack = nullptr;
    QWidget *m_previewPage = nullptr;
    DrivingImageSettingsPage *m_settingsPage = nullptr;
    DrivingImagePlaybackPage *m_playbackPage = nullptr;
    DrivingImageNavBar *m_navBar = nullptr;
    DrivingImagePreviewTopBar *m_previewTopBar = nullptr;

    QFrame *m_previewWrap;
    QFrame *m_safetyTipFrame;
    QLabel *m_safetyTipIcon;
    QLabel *m_safetyTipText;
    QLabel *m_exitHintLabel;
    QLabel *m_longPressHintLabel = nullptr;
    AhdManager *m_ahdManager = nullptr;
    AhdManager *ahdManager();
    QTimer *m_singleClickTimer;
    QTimer *m_longPressTimer = nullptr;
    bool m_previewChromeVisible = false;
    bool m_longPressTriggered = false;
    bool m_returning;
    bool m_exitInProgress;
    bool m_startScheduled;
    bool m_isFullscreen;
    int m_fullscreenCameraId;
    int m_cameraMode;
    QPoint m_pendingClickGlobalPos;
    qint64 m_lastClickMs;
    QPoint m_lastClickPos;
};

#endif // DRIVINGIMAGEWINDOW_H
