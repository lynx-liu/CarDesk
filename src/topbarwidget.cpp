#include "topbarwidget.h"
#include "appsignals.h"
#include "t507sdkbridge.h"

#include <QHBoxLayout>
#include <QDateTime>
#include <QApplication>
#include <QVariant>
#include <QStorageInfo>

TopBarRightWidget::TopBarRightWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent;");

    auto *outerLay = new QHBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(16);

    // ── 蓝牙图标 ────────────────────────────────────────────────────────────
    m_btBtn = new QPushButton(this);
    m_btBtn->setFixedSize(48, 48);
    m_btBtn->setFocusPolicy(Qt::NoFocus);
    m_btBtn->setCursor(Qt::PointingHandCursor);
    m_btBtn->setToolTip("蓝牙");
    outerLay->addWidget(m_btBtn);

    const bool initialBtConnected = qApp->property("appBluetoothConnected").toBool();
    onBluetoothStateChanged(initialBtConnected);
    connect(AppSignals::instance(), &AppSignals::bluetoothConnectedChanged,
            this, &TopBarRightWidget::onBluetoothStateChanged);

    // ── USB 图标 ─────────────────────────────────────────────────────────────
    m_usbBtn = new QPushButton(this);
    m_usbBtn->setFixedSize(48, 48);
    m_usbBtn->setFocusPolicy(Qt::NoFocus);
    m_usbBtn->setCursor(Qt::ArrowCursor);
    m_usbBtn->setToolTip("USB");
    m_usbBtn->setStyleSheet(
        "QPushButton { border: none; background-image: url(:/images/pict_usb.png); "
        "background-repeat: no-repeat; background-position: center; }"
    );
    outerLay->addWidget(m_usbBtn);

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
    // ── USB 插拔检测定时器 ─────────────────────────────────────────────────────
    m_usbTimer = new QTimer(this);
    m_usbTimer->setInterval(1000);
    connect(m_usbTimer, &QTimer::timeout, this, &TopBarRightWidget::updateUsbState);
    m_usbTimer->start();
    updateUsbState();
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
        // app level 0 对应静音
        m_isMuted = true;
        if (m_volBtn) {
            m_volBtn->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/pict_volume_mute.png); "
                "background-repeat: no-repeat; background-position: center; }");
        }
        if (m_volLabel) m_volLabel->setText("");
    } else {
        // app level 1..10 对应有声音状态
        m_isMuted = false;
        if (m_volBtn) {
            m_volBtn->setStyleSheet(
                "QPushButton { border: none; background-image: url(:/images/pict_volume.png); "
                "background-repeat: no-repeat; background-position: center; }");
        }
        if (m_volLabel) m_volLabel->setText(QString::number(bounded));
    }
}

void TopBarRightWidget::onBluetoothStateChanged(bool connected)
{
    if (!m_btBtn)
        return;

    const QString icon = connected
        ? QStringLiteral(":/images/pict_bluetooth_on.png")
        : QStringLiteral(":/images/pict_bluetooth.png");
    m_btBtn->setStyleSheet(
        QString("QPushButton { border: none; background-image: url(%1); "
                "background-repeat: no-repeat; background-position: center; }"
                "QPushButton:hover { background-image: url(%1); }").arg(icon));
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
    if (m_isMuted) {
        const QVariant cur = qApp->property("appVolumeLevel");
        const int curLv = cur.isValid() ? cur.toInt() : 0;
        qApp->setProperty("appVolumePrevLevel", curLv);
        qApp->setProperty("appVolumeLevel", 0);
        AppSignals::runAmixer({"sset", "digital volume", QString::number(63)}, this);
    } else {
        const QVariant prev = qApp->property("appVolumePrevLevel");
        const int restore = prev.isValid() ? prev.toInt() : 10;
        qApp->setProperty("appVolumeLevel", restore);
        AppSignals::runAmixer({"sset", "digital volume", QString::number(AppSignals::volumeDigitalFromLevel(restore))}, this);
    }
}

void TopBarRightWidget::onClockTick()
{
    if (m_timeLabel) {
        m_timeLabel->setText(QDateTime::currentDateTime().toString(AppSignals::timeFormat()));
    }
}

void TopBarRightWidget::updateUsbState()
{
    bool foundUsb = false;

    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady())
            continue;

        const QString root = storage.rootPath();
        if (root == QStringLiteral("/mnt/usb") || root.startsWith(QStringLiteral("/mnt/usb/"))) {
            foundUsb = true;
            break;
        }
    }

    if (foundUsb == m_usbConnected)
        return;

    m_usbConnected = foundUsb;
    if (!m_usbBtn)
        return;

    const QString icon = m_usbConnected
        ? QStringLiteral(":/images/pict_usb_on.png")
        : QStringLiteral(":/images/pict_usb.png");
    m_usbBtn->setStyleSheet(
        QString("QPushButton { border: none; background-image: url(%1); "
                "background-repeat: no-repeat; background-position: center; }")
            .arg(icon));
}
