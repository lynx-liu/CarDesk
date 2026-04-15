#include "topbarwidget.h"
#include "appsignals.h"

#include <QHBoxLayout>
#include <QDateTime>
#include <QApplication>
#include <QVariant>

TopBarRightWidget::TopBarRightWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent;");

    auto *outerLay = new QHBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(16);

    // ── 蓝牙图标 ────────────────────────────────────────────────────────────
    auto *btBtn = new QPushButton(this);
    btBtn->setFixedSize(48, 48);
    btBtn->setFocusPolicy(Qt::NoFocus);
    btBtn->setCursor(Qt::PointingHandCursor);
    btBtn->setToolTip("蓝牙");
    btBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_buetooth.png); "
        "background-repeat: no-repeat; background-position: center; }"
        "QPushButton:hover { background-image: url(:/images/pict_buetooth_on.png); }");
    outerLay->addWidget(btBtn);

    // ── USB 图标 ─────────────────────────────────────────────────────────────
    auto *usbBtn = new QPushButton(this);
    usbBtn->setFixedSize(48, 48);
    usbBtn->setFocusPolicy(Qt::NoFocus);
    usbBtn->setCursor(Qt::PointingHandCursor);
    usbBtn->setToolTip("USB");
    usbBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_usb.png); "
        "background-repeat: no-repeat; background-position: center; }"
        "QPushButton:hover { background-image: url(:/images/pict_usb_on.png); }");
    outerLay->addWidget(usbBtn);

    // ── 音量图标 + 数值（合为一个子 widget，间距 6px） ───────────────────────
    auto *volGroup = new QWidget(this);
    volGroup->setStyleSheet("background: transparent;");
    auto *volGroupLay = new QHBoxLayout(volGroup);
    volGroupLay->setContentsMargins(0, 0, 0, 0);
    volGroupLay->setSpacing(6);

    m_volBtn = new QPushButton(volGroup);
    m_volBtn->setFixedSize(48, 48);
    m_volBtn->setFocusPolicy(Qt::NoFocus);
    m_volBtn->setCursor(Qt::PointingHandCursor);
    m_volBtn->setToolTip("音量");
    m_volBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_volume.png); "
        "background-repeat: no-repeat; background-position: center; }");
    connect(m_volBtn, &QPushButton::clicked, this, &TopBarRightWidget::onVolumeBtnClicked);
    volGroupLay->addWidget(m_volBtn);

    m_volLabel = new QLabel(volGroup);
    m_volLabel->setFixedWidth(52);   // 固定宽，静音/取消不移位图标
    m_volLabel->setStyleSheet("QLabel { color: #fff; font-size: 36px; background: transparent; }");
    volGroupLay->addWidget(m_volLabel);

    outerLay->addWidget(volGroup);

    // ── 时间 ─────────────────────────────────────────────────────────────────
    m_timeLabel = new QLabel(this);
    m_timeLabel->setFixedWidth(150);
    m_timeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_timeLabel->setStyleSheet("QLabel { color: #fff; font-size: 36px; background: transparent; }");
    outerLay->addWidget(m_timeLabel);

    // ── 初始化显示 ────────────────────────────────────────────────────────────
    const QVariant vp = qApp->property("appVolumeLevel");
    onVolumeChanged(vp.isValid() ? vp.toInt() : 10);
    onClockTick();

    // ── 连接全局音量信号 ──────────────────────────────────────────────────────
    connect(AppSignals::instance(), &AppSignals::volumeLevelChanged,
            this, &TopBarRightWidget::onVolumeChanged);

    // 时钟制式变化时立即刷新显示
    connect(AppSignals::instance(), &AppSignals::clockFormatChanged,
            this, [this](bool) { onClockTick(); });

    // ── 时钟定时器 ────────────────────────────────────────────────────────────
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &TopBarRightWidget::onClockTick);
    m_clockTimer->start();
}

void TopBarRightWidget::onVolumeChanged(int level)
{
    const int bounded = qBound(0, level, 10);
    if (bounded == 0) {
        // 硬件音量降至 0：自动切换为静音图标
        m_isMuted = true;
        if (m_volBtn) {
            m_volBtn->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/pict_volume_mute.png); "
                "background-repeat: no-repeat; background-position: center; }");
        }
        if (m_volLabel) m_volLabel->setText("");
    } else {
        // 硬件音量 > 0：恢复音量图标 + 数字（同时取消手动静音状态）
        m_isMuted = false;
        if (m_volBtn) {
            m_volBtn->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/pict_volume.png); "
                "background-repeat: no-repeat; background-position: center; }");
        }
        if (m_volLabel) m_volLabel->setText(QString::number(bounded));
    }
}

void TopBarRightWidget::onVolumeBtnClicked()
{
    m_isMuted = !m_isMuted;
    if (m_volBtn) {
        const QString icon = m_isMuted
            ? QStringLiteral(":/images/pict_volume_mute.png")
            : QStringLiteral(":/images/pict_volume.png");
        m_volBtn->setStyleSheet(
            QString("QPushButton { border: none; background-image: url(%1); "
                    "background-repeat: no-repeat; background-position: center; }").arg(icon));
    }
    if (m_volLabel) {
        if (m_isMuted) {
            m_volLabel->setText("");
        } else {
            const QVariant vp = qApp->property("appVolumeLevel");
            m_volLabel->setText(QString::number(vp.isValid() ? vp.toInt() : 10));
        }
    }
}

void TopBarRightWidget::onClockTick()
{
    if (m_timeLabel) {
        m_timeLabel->setText(QDateTime::currentDateTime().toString(AppSignals::timeFormat()));
    }
}
