#include "topbarwidget.h"
#include "appsignals.h"
#include "t507sdkbridge.h"

#include <QHBoxLayout>
#include <QDateTime>
#include <QApplication>

static const QString kUsbMountDir    = QStringLiteral("/mnt/usb");
static const QString kUsbMountPrefix = QStringLiteral("/mnt/usb/");
#include <QVariant>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QFileInfo>
#include <QRegularExpression>
#include <QQueue>
#include <fcntl.h>
#include <unistd.h>

// --- USBtest核心检测逻辑移植 ---
static bool isAnyUsbStoragePresentAndCleanup(const QString &mountBaseDir = QStringLiteral("/mnt/usb")) {
    // 1. 枚举所有USB设备，查找有效block设备
    QSet<QString> validBlocks;
    auto addBlockAndPartitions = [&](const QString &blk) {
        if (validBlocks.contains(blk))
            return;
        validBlocks.insert(blk);
        QDir blockDir(QStringLiteral("/sys/block/%1").arg(blk));
        if (!blockDir.exists())
            return;
        QRegularExpression partRegex(QStringLiteral("^%1(?:\\d+|p\\d+)$").arg(QRegularExpression::escape(blk)));
        const QStringList parts = blockDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &part : parts) {
            if (partRegex.match(part).hasMatch()) {
                validBlocks.insert(part);
            }
        }
    };

    QDir sysUsbDir("/sys/bus/usb/devices");
    const QStringList devEntries = sysUsbDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &devName : devEntries) {
        QString devPath = "/sys/bus/usb/devices/" + devName;
        // 跳过root hub
        if (!QFile::exists(devPath + "/idVendor")) continue;
        QString devClass;
        QFile classFile(devPath + "/bDeviceClass");
        if (classFile.open(QIODevice::ReadOnly)) {
            devClass = QString::fromLatin1(classFile.readLine()).trimmed();
            classFile.close();
        }
        if (devClass == "09") continue; // root hub
        // 查找block设备（USBtest风格）
        QDir devDir(devPath);
        const QStringList subDirs = devDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        bool foundBlockForDevice = false;
        for (const QString &sub : subDirs) {
            QString blockPath = devPath + "/" + sub + "/block";
            QDir blkDir(blockPath);
            if (blkDir.exists()) {
                const QStringList blks = blkDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString &blk : blks) {
                    QString devNode = "/dev/" + blk;
                    QFileInfo fi(devNode);
                    bool valid = false;
                    if (fi.exists()) {
                        int fd = ::open(devNode.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
                        if (fd >= 0) {
                            valid = true;
                            ::close(fd);
                        }
                    }
                    if (valid) {
                        addBlockAndPartitions(blk);
                        foundBlockForDevice = true;
                    }
                }
            }
            // host分支
            QString hostBase = devPath + "/" + sub;
            QDir hostDir(hostBase);
            const QStringList hostEntries = hostDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &host : hostEntries) {
                if (!host.startsWith("host")) continue;
                QString targetBase = hostBase + "/" + host;
                QDir targetDir(targetBase);
                const QStringList targetEntries = targetDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString &target : targetEntries) {
                    if (!target.startsWith("target")) continue;
                    QString scsiBase = targetBase + "/" + target;
                    QDir scsiDir(scsiBase);
                    const QStringList scsiEntries = scsiDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const QString &scsi : scsiEntries) {
                        QString blkPath = scsiBase + "/" + scsi + "/block";
                        QDir blkDir2(blkPath);
                        if (blkDir2.exists()) {
                            const QStringList blks = blkDir2.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                            for (const QString &blk : blks) {
                                QString devNode = "/dev/" + blk;
                                QFileInfo fi(devNode);
                                bool valid = false;
                                if (fi.exists()) {
                                    int fd = ::open(devNode.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
                                    if (fd >= 0) {
                                        valid = true;
                                        ::close(fd);
                                    }
                                }
                                if (valid) {
                                    addBlockAndPartitions(blk);
                                    foundBlockForDevice = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!foundBlockForDevice) {
            QStringList allBlocks = QDir("/sys/block").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &blk : allBlocks) {
                QString link = QFile::symLinkTarget(QString("/sys/block/%1").arg(blk));
                if (link.contains(devName)) {
                    QString devNode = "/dev/" + blk;
                    QFileInfo fi(devNode);
                    bool valid = false;
                    if (fi.exists()) {
                        int fd = ::open(devNode.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
                        if (fd >= 0) {
                            valid = true;
                            ::close(fd);
                        }
                    }
                    if (valid) {
                        addBlockAndPartitions(blk);
                    }
                }
            }
        }
    }
    // 2. 检查/mnt/usb下是否有有效block设备目录，并清理无效空目录
    bool foundUsb = false;
    QDir usbRoot(mountBaseDir);
    if (usbRoot.exists()) {
        const QStringList subs = usbRoot.entryList(QDir::NoDotAndDotDot | QDir::Dirs, QDir::Name);
        for (const QString &sub : subs) {
            if (validBlocks.contains(sub)) {
                foundUsb = true;
            } else {
                // 目录为空且无block设备，自动清理
                QDir mountDir(mountBaseDir + "/" + sub);
                QStringList contents = mountDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
                if (contents.isEmpty()) {
                    mountDir.rmdir(".");
                }
            }
        }
    }
    return foundUsb;
}

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
    m_usbTimer->setInterval(5000); // 兜底轮询，主要靠 QFileSystemWatcher 即时触发
    connect(m_usbTimer, &QTimer::timeout, this, &TopBarRightWidget::updateUsbState);
    m_usbTimer->start();

    // USB 挂载防抖定时器：/proc/mounts 变化后等待文件系统真正就绪再检测状态。
    // NTFS/exFAT 在 ARM 上挂载可能需要 3-8 秒，若立即 entryInfoList() 会阻塞 UI 线程。
    m_usbDebounceTimer = new QTimer(this);
    m_usbDebounceTimer->setSingleShot(true);
    m_usbDebounceTimer->setInterval(1500); // 等待 1.5 秒让内核完成挂载
    connect(m_usbDebounceTimer, &QTimer::timeout, this, &TopBarRightWidget::updateUsbState);

    // 监听u盘目录变化，USB 插拔时立即响应
    m_mountsWatcher = new QFileSystemWatcher(this);
    m_mountsWatcher->addPath(QStringLiteral("/proc/mounts"));
    if (QDir(QStringLiteral("/mnt")).exists()) {
        m_mountsWatcher->addPath(QStringLiteral("/mnt"));
    }
    if (QDir(kUsbMountDir).exists()) {
        m_mountsWatcher->addPath(kUsbMountDir);
    }
    connect(m_mountsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // 重置防抖计时器——多次触发合并为一次延迟检测，等文件系统真正就绪
        m_usbDebounceTimer->start();
        // /proc/mounts 变化后 inotify watch 可能失效，重新添加
        if (!m_mountsWatcher->files().contains(path))
            m_mountsWatcher->addPath(path);
    });
    connect(m_mountsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        m_usbDebounceTimer->start();
    });

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
    AppSignals::toggleMute();
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

    // 直接读 /proc/mounts，挂载点只认 /mnt/usb/xxxx
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!mounts.atEnd()) {
            const QByteArray raw = mounts.readLine();
            const int sp1 = raw.indexOf(' ');
            if (sp1 < 0) continue;
            const int sp2 = raw.indexOf(' ', sp1 + 1);
            const QString mnt = (sp2 > sp1)
                ? QString::fromLatin1(raw.mid(sp1 + 1, sp2 - sp1 - 1))
                : QString::fromLatin1(raw.mid(sp1 + 1)).trimmed();
            if (mnt.startsWith(kUsbMountPrefix)) {
                foundUsb = true;
                break;
            }
        }
        mounts.close();
    }

    // 完全移植USBtest的U盘有效性判定逻辑
    if (!foundUsb) {
        foundUsb = isAnyUsbStoragePresentAndCleanup(kUsbMountDir);
    }

    if (foundUsb == m_usbConnected)
        return;

    m_usbConnected = foundUsb;
    emit AppSignals::instance()->usbStateChanged(foundUsb);
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
