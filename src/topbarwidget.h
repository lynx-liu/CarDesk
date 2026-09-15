#ifndef TOPBARWIDGET_H
#define TOPBARWIDGET_H

#include "appsettings.h"

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFileSystemWatcher>

/**
 * TopBarRightWidget — 顶部栏右侧状态图标组件
 *
 * 统一封装：BT图标 / 漏气图标 / USB图标 / 音量图标(QPushButton) / 音量数值(QLabel,固定宽度)
 *
 * 通过 AppSignals::volumeLevelChanged 信号自动同步音量显示，无需外部调用。
 *
 * 使用方式（绝对定位窗口）：
 *   auto *right = new TopBarRightWidget(topBar);
 *   right->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
 *                      TopBarRightWidget::preferredWidth(), 48);
 *
 * 使用方式（布局窗口）：
 *   layout->addWidget(new TopBarRightWidget(this), 0, 2, Qt::AlignRight | Qt::AlignVCenter);
 */
class TopBarRightWidget : public QWidget {
    Q_OBJECT
public:
    explicit TopBarRightWidget(QWidget *parent = nullptr);

    /** 建议的固定宽度（像素）：BT+漏气位+USB+vol组+间距（漏气位始终预留） */
    static int preferredWidth() {
        return AppSettings::debugMode() ? 298 : 234;
    }

private slots:
    void onVolumeChanged(int level);
    void onVolumeBtnClicked();
    void onBluetoothStateChanged(bool connected);
    void updateUsbState();
    void onTpmsLeakWarningChanged(bool active);

private:
    void setLeakIconVisible(bool visible);

    QPushButton *m_btBtn    = nullptr;
    QLabel      *m_leakLab  = nullptr;
    QPushButton *m_usbBtn   = nullptr;
    QPushButton *m_volBtn    = nullptr;
    QLabel      *m_volLabel  = nullptr;
    QTimer             *m_usbTimer = nullptr;
    QTimer             *m_usbDebounceTimer = nullptr;  // /proc/mounts 变化后防抖延迟
    QFileSystemWatcher *m_mountsWatcher = nullptr;
    bool         m_usbConnected = false;
    bool         m_isMuted   = false;
};

#endif // TOPBARWIDGET_H
