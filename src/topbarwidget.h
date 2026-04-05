#ifndef TOPBARWIDGET_H
#define TOPBARWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

/**
 * TopBarRightWidget — 顶部栏右侧状态图标组件
 *
 * 统一封装：BT图标 / USB图标 / 音量图标(QPushButton) / 音量数值(QLabel,固定宽度)
 *           / 时钟(QLabel,每秒刷新)
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

    /** 建议的固定宽度（像素）：BT+USB+vol组+时间+间距之和 */
    static int preferredWidth() { return 344; }

private slots:
    void onVolumeChanged(int level);
    void onVolumeBtnClicked();
    void onClockTick();

private:
    QPushButton *m_volBtn    = nullptr;
    QLabel      *m_volLabel  = nullptr;
    QLabel      *m_timeLabel = nullptr;
    QTimer      *m_clockTimer = nullptr;
    bool         m_isMuted   = false;
};

#endif // TOPBARWIDGET_H
