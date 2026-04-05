#ifndef DRIVINGIMAGEWINDOW_H
#define DRIVINGIMAGEWINDOW_H

#include <QMainWindow>

#include "ahdmanager.h"

class QFrame;
class QHideEvent;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QTimer;

class DrivingImageWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DrivingImageWindow(QWidget *parent = nullptr);
    void warmupCamera();

signals:
    void requestReturnToMain();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void returnToMainSafely();
    void setupUI();
    void bindAhdSignals();
    void layoutCenterHint();
    void handleConfirmedSingleClick(const QPoint &globalPos);
    void startPreviewIfNeeded();
    void stopPreview();
    QRect previewRectOnScreen() const;
    void setLoadingState(bool loading);

    QFrame *m_previewWrap;
    QLabel *m_exitHintLabel;
    AhdManager *m_ahdManager;
    QTimer *m_singleClickTimer;
    bool m_returning;
    bool m_previewLoading;
    bool m_exitInProgress;
    bool m_startScheduled;
    bool m_isFullscreen;
    int m_fullscreenCameraId;
    QPoint m_pendingClickGlobalPos;
    qint64 m_lastClickMs;
    QPoint m_lastClickPos;
};

#endif // DRIVINGIMAGEWINDOW_H
