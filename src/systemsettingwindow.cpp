#include "systemsettingwindow.h"
#include "otamanager.h"
#include "devicedetect.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QScreen>
#include <QTime>
#include <QVector>
#include <QRegExp>

namespace {
QString shellQuote(const QString &s)
{
    QString out = s;
    out.replace("'", "'\\''");
    return "'" + out + "'";
}
}

SystemSettingWindow::SystemSettingWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_pages(nullptr)
    , m_subnavList(nullptr)
    , m_updateStateLabel(nullptr)
    , m_updateProgressText(nullptr)
    , m_updateProgressBar(nullptr)
    , m_updateIntroLabel(nullptr)
    , m_updateProgressRowWidget(nullptr)
    , m_updateStartBtn(nullptr)
    , m_updateCancelBtn(nullptr)
    , m_updateProgress(0)
    , m_selectedModule(0)
    , m_updateTimer(new QTimer(this))
    , m_firmwareIntroLabel(nullptr)
    , m_firmwareStateLabel(nullptr)
    , m_firmwareVersionLabel(nullptr)
    , m_firmwareProgressText(nullptr)
    , m_firmwareProgressBar(nullptr)
    , m_firmwareProgressRowWidget(nullptr)
    , m_firmwareStartBtn(nullptr)
    , m_firmwareCancelBtn(nullptr)
    , m_updateTabWidget(nullptr)
    , m_otaManager(new OTAManager(this))
{
    setWindowTitle(QStringLiteral("系统设置"));
    setObjectName("systemSettingWindow");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();

    m_updateTimer->setInterval(120);
    connect(m_updateTimer, &QTimer::timeout, this, &SystemSettingWindow::onTickUpdate);
    
    // 连接OTA管理器信号
    connect(m_otaManager, &OTAManager::updateProgress, this, &SystemSettingWindow::onUpdateProgress);
    connect(m_otaManager, &OTAManager::updateStateChanged, this, &SystemSettingWindow::onUpdateStateChanged);
    connect(m_otaManager, &OTAManager::updateStarted, this, &SystemSettingWindow::onUpdateStarted);
    connect(m_otaManager, &OTAManager::updateCompleted, this, &SystemSettingWindow::onUpdateCompleted);
    connect(m_otaManager, &OTAManager::updateFailed, this, &SystemSettingWindow::onUpdateFailed);
    connect(m_otaManager, &OTAManager::updateCancelled, this, &SystemSettingWindow::onUpdateCancelled);
}

void SystemSettingWindow::closeEvent(QCloseEvent *event)
{
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void SystemSettingWindow::onSubnavChanged(int index)
{
    if (!m_pages) {
        return;
    }
    m_pages->setCurrentIndex(index);
}

void SystemSettingWindow::onStartUpdate()
{
    if (!m_updateStateLabel || !m_updateProgressText || !m_updateProgressBar) {
        return;
    }

    m_updateProgress = 0;
    m_updateProgressBar->setValue(0);
    m_updateProgressText->setText(QStringLiteral("0%"));
    m_updateStateLabel->setText(QStringLiteral("正在查找U盘升级包..."));

    if (m_updateIntroLabel) {
        m_updateIntroLabel->hide();
    }
    if (m_updateStartBtn) {
        m_updateStartBtn->hide();
    }
    if (m_updateProgressRowWidget) {
        m_updateProgressRowWidget->show();
    }
    if (m_updateStateLabel) {
        m_updateStateLabel->show();
    }
    if (m_updateCancelBtn) {
        m_updateCancelBtn->show();
    }

    QString usbRoot;
    const QString archivePath = findAppUpdateArchive(&usbRoot);
    if (archivePath.isEmpty()) {
        m_updateProgressBar->setValue(0);
        m_updateProgressText->setText(QStringLiteral("0%"));
        m_updateStateLabel->setText(QStringLiteral("未找到应用升级包\n请将 CarDesk_bundle*.tar.gz 放到U盘根目录"));
        if (m_updateCancelBtn) {
            m_updateCancelBtn->hide();
        }
        if (m_updateStartBtn) {
            m_updateStartBtn->show();
        }
        return;
    }

    m_updateProgress = 20;
    m_updateProgressBar->setValue(m_updateProgress);
    m_updateProgressText->setText(QStringLiteral("%1%").arg(m_updateProgress));
    const QString archiveName = QFileInfo(archivePath).fileName();
    m_updateStateLabel->setText(QStringLiteral("已找到升级包，准备解压：\n%1").arg(archiveName));

    QString error;
    if (!applyAppUpdateFromArchive(archivePath, &error)) {
        m_updateProgressBar->setValue(0);
        m_updateProgressText->setText(QStringLiteral("0%"));
        m_updateStateLabel->setText(QStringLiteral("应用升级失败：\n%1").arg(error));
        if (m_updateCancelBtn) {
            m_updateCancelBtn->hide();
        }
        if (m_updateStartBtn) {
            m_updateStartBtn->show();
        }
        return;
    }

    m_updateProgress = 100;
    m_updateProgressBar->setValue(m_updateProgress);
    m_updateProgressText->setText(QStringLiteral("100%"));
    m_updateStateLabel->setText(QStringLiteral("应用升级成功，正在自动重启程序..."));
    if (m_updateCancelBtn) {
        m_updateCancelBtn->hide();
    }
    if (m_updateStartBtn) {
        m_updateStartBtn->show();
    }

    QTimer::singleShot(1200, this, []() {
        bool started = QProcess::startDetached(QStringLiteral("/usr/bin/run.sh"));
        if (!started) {
            started = QProcess::startDetached(QApplication::applicationFilePath());
        }
        if (started) {
            QApplication::quit();
        }
    });
}

void SystemSettingWindow::onCancelUpdate()
{
    m_updateTimer->stop();
    if (m_updateStateLabel) {
        m_updateStateLabel->setText(QStringLiteral("更新已取消"));
    }
    if (m_updateProgressRowWidget) {
        m_updateProgressRowWidget->hide();
    }
    if (m_updateCancelBtn) {
        m_updateCancelBtn->hide();
    }
    if (m_updateIntroLabel) {
        m_updateIntroLabel->show();
    }
    if (m_updateStartBtn) {
        m_updateStartBtn->show();
    }
}

void SystemSettingWindow::onTickUpdate()
{
    if (!m_updateProgressBar || !m_updateProgressText || !m_updateStateLabel) {
        return;
    }

    m_updateProgress += 3;
    if (m_updateProgress >= 100) {
        m_updateProgress = 100;
        m_updateTimer->stop();
        m_updateStateLabel->setText(QStringLiteral("模块 %1 更新完成").arg(m_selectedModule + 1));
        if (m_updateCancelBtn) {
            m_updateCancelBtn->hide();
        }
        if (m_updateStartBtn) {
            m_updateStartBtn->show();
        }
    }

    m_updateProgressBar->setValue(m_updateProgress);
    m_updateProgressText->setText(QStringLiteral("%1%").arg(m_updateProgress));
}

QString SystemSettingWindow::findAppUpdateArchive(QString *usbRoot) const
{
    const QStringList usbPaths = {
        QStringLiteral("/mnt/usb"),
        QStringLiteral("/mnt/usb0"),
        QStringLiteral("/media/usb"),
        QStringLiteral("/media/usb0"),
        QStringLiteral("/run/media"),
        QStringLiteral("/mnt")
    };

    for (const QString &basePath : usbPaths) {
        QDir baseDir(basePath);
        if (!baseDir.exists()) {
            continue;
        }

        QDirIterator it(basePath,
                        QStringList() << QStringLiteral("CarDesk_bundle*.tar.gz") << QStringLiteral("CarDesk_bundle*.tgz"),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        if (it.hasNext()) {
            const QString hit = it.next();
            if (usbRoot) {
                *usbRoot = basePath;
            }
            return hit;
        }
    }

    return QString();
}

bool SystemSettingWindow::applyAppUpdateFromArchive(const QString &archivePath, QString *errorMessage)
{
    const QString tmpDir = QStringLiteral("/tmp/cardesk_app_update");

    m_updateProgress = 40;
    m_updateProgressBar->setValue(m_updateProgress);
    m_updateProgressText->setText(QStringLiteral("%1%").arg(m_updateProgress));
    m_updateStateLabel->setText(QStringLiteral("正在解压升级包..."));

    {
        QProcess cleanProc;
        cleanProc.start(QStringLiteral("sh"), QStringList() << QStringLiteral("-c")
            << QStringLiteral("rm -rf %1 && mkdir -p %1").arg(shellQuote(tmpDir)));
        cleanProc.waitForFinished(-1);
        if (cleanProc.exitStatus() != QProcess::NormalExit || cleanProc.exitCode() != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法创建临时目录 /tmp/cardesk_app_update");
            }
            return false;
        }
    }

    {
        auto runShell = [](const QString &cmd, QString *mergedOutput) -> bool {
            QProcess proc;
            proc.setProcessChannelMode(QProcess::MergedChannels);
            proc.start(QStringLiteral("sh"), QStringList() << QStringLiteral("-c") << cmd);
            proc.waitForFinished(-1);
            if (mergedOutput) {
                *mergedOutput = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
            }
            return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        };

        QString errLog;
        // BusyBox tar 不支持 -z，仅使用 gzip 管道解压 .tar.gz
        const QString cmdGzipDc = QStringLiteral("gzip -dc %1 | tar -xf - -C %2")
                                      .arg(shellQuote(archivePath), shellQuote(tmpDir));
        const bool ok = runShell(cmdGzipDc, &errLog);

        if (!ok) {
            if (errorMessage) {
                QString detail = errLog;
                if (detail.isEmpty()) {
                    detail = QStringLiteral("请确认系统包含 gzip，并检查升级包是否完整");
                }
                if (detail.size() > 180) {
                    detail = detail.left(180) + QStringLiteral("...");
                }
                *errorMessage = QStringLiteral("解压失败：%1").arg(detail);
            }
            return false;
        }
    }

    QString newBinary;
    QDirIterator scanIt(tmpDir, QDir::Files, QDirIterator::Subdirectories);
    while (scanIt.hasNext()) {
        const QString filePath = scanIt.next();
        const QFileInfo fi(filePath);
        if (fi.fileName() == QStringLiteral("CarDesk") && newBinary.isEmpty()) {
            newBinary = filePath;
        }
    }

    if (newBinary.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("升级包中未找到 CarDesk 可执行文件");
        }
        return false;
    }

    m_updateProgress = 75;
    m_updateProgressBar->setValue(m_updateProgress);
    m_updateProgressText->setText(QStringLiteral("%1%").arg(m_updateProgress));
    m_updateStateLabel->setText(QStringLiteral("正在替换系统程序..."));

    const QFileInfo binInfo(newBinary);
    const QString srcDir = binInfo.absolutePath();
    const QString syncCmd = QStringLiteral("cp -af %1/. /usr/bin/").arg(shellQuote(srcDir));

    QProcess syncProc;
    syncProc.start(QStringLiteral("sh"), QStringList() << QStringLiteral("-c") << syncCmd);
    syncProc.waitForFinished(-1);
    if (syncProc.exitStatus() != QProcess::NormalExit || syncProc.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目录覆盖失败，请确认有 /usr/bin 写权限");
        }
        return false;
    }

    QProcess chmodProc;
    chmodProc.start(QStringLiteral("chmod"), QStringList() << QStringLiteral("+x") << QStringLiteral("/usr/bin/CarDesk"));
    chmodProc.waitForFinished(-1);

    return true;
}

void SystemSettingWindow::onUpdateProgress(int percentage)
{
    if (m_firmwareProgressBar) {
        m_firmwareProgressBar->setValue(percentage);
    }
    if (m_firmwareProgressText) {
        m_firmwareProgressText->setText(QStringLiteral("%1%").arg(percentage));
    }
}

void SystemSettingWindow::onUpdateStateChanged(const QString &state)
{
    if (m_firmwareStateLabel) {
        m_firmwareStateLabel->setText(state);
    }
    qDebug() << "Update state:" << state;
}

void SystemSettingWindow::onUpdateStarted()
{
    if (m_firmwareIntroLabel) m_firmwareIntroLabel->hide();
    if (m_firmwareStartBtn) m_firmwareStartBtn->hide();
    if (m_firmwareStateLabel) {
        m_firmwareStateLabel->setText(QStringLiteral("正在更新..."));
        m_firmwareStateLabel->show();
    }
    if (m_firmwareProgressRowWidget) m_firmwareProgressRowWidget->show();
    if (m_firmwareCancelBtn) m_firmwareCancelBtn->show();
}

void SystemSettingWindow::onUpdateCompleted()
{
    if (m_firmwareStateLabel) {
        m_firmwareStateLabel->setText(QStringLiteral("升级成功！系统将在3秒后重启..."));
        m_firmwareStateLabel->show();
    }
    if (m_firmwareProgressRowWidget) m_firmwareProgressRowWidget->hide();
    if (m_firmwareCancelBtn) m_firmwareCancelBtn->hide();
    if (m_firmwareIntroLabel) m_firmwareIntroLabel->hide();
    if (m_firmwareStartBtn) m_firmwareStartBtn->hide();
}

void SystemSettingWindow::onUpdateFailed(const QString &error)
{
    if (m_firmwareStateLabel) {
        m_firmwareStateLabel->setText(QStringLiteral("升级失败: %1").arg(error));
        m_firmwareStateLabel->show();
    }
    if (m_firmwareProgressRowWidget) m_firmwareProgressRowWidget->hide();
    if (m_firmwareCancelBtn) m_firmwareCancelBtn->hide();
    if (m_firmwareIntroLabel) m_firmwareIntroLabel->show();
    if (m_firmwareStartBtn) m_firmwareStartBtn->show();
}

void SystemSettingWindow::onUpdateCancelled()
{
    if (m_firmwareStateLabel) {
        m_firmwareStateLabel->setText(QStringLiteral("升级已取消"));
        m_firmwareStateLabel->show();
    }
    if (m_firmwareProgressRowWidget) m_firmwareProgressRowWidget->hide();
    if (m_firmwareCancelBtn) m_firmwareCancelBtn->hide();
    if (m_firmwareIntroLabel) m_firmwareIntroLabel->show();
    if (m_firmwareStartBtn) m_firmwareStartBtn->show();
}

void SystemSettingWindow::onFirmwareCheckUpdate()
{
    if (!m_firmwareStartBtn) {
        return;
    }

    m_checkedUpdateFile.clear();
    m_checkedNewVersion.clear();

    // 检查USB中是否有升级文件
    QString updateFile;
    bool found = m_otaManager->checkUpdateFile(updateFile);

    if (found) {
        m_checkedUpdateFile = updateFile;
        const QString currentVersion = m_otaManager->getCurrentVersion();
        const QString newVersion = m_otaManager->parseVersionFromSWU(updateFile).trimmed();
        const QString currentPart = m_otaManager->getCurrentPartition();
        const QString targetPart = (currentPart == QStringLiteral("bootA"))
                                       ? QStringLiteral("bootB")
                                       : (currentPart == QStringLiteral("bootB") ? QStringLiteral("bootA") : QStringLiteral("未知"));

        bool allowStart = true;
        if (!newVersion.isEmpty() && !currentVersion.isEmpty()) {
            auto versionToList = [](const QString &v) {
                QVector<int> out;
                const QStringList parts = v.split(QRegExp("[^0-9]+"), QString::SkipEmptyParts);
                for (const QString &p : parts) {
                    out.push_back(p.toInt());
                }
                return out;
            };

            const QVector<int> cur = versionToList(currentVersion);
            const QVector<int> nxt = versionToList(newVersion);
            const int n = qMax(cur.size(), nxt.size());
            int cmp = 0;
            for (int i = 0; i < n; ++i) {
                const int cv = (i < cur.size()) ? cur[i] : 0;
                const int nv = (i < nxt.size()) ? nxt[i] : 0;
                if (nv < cv) {
                    cmp = -1;
                    break;
                }
                if (nv > cv) {
                    cmp = 1;
                    break;
                }
            }
            if (cmp <= 0) {
                allowStart = false;
            }
        }

        if (!newVersion.isEmpty()) {
            m_checkedNewVersion = newVersion;
        }

        if (m_firmwareStateLabel) {
            QString state = QStringLiteral("找到升级文件: %1\n当前版本: %2\n升级包版本: %3\n关联升级路径: %4 -> %5")
                                .arg(updateFile)
                                .arg(currentVersion.isEmpty() ? QStringLiteral("未知") : currentVersion)
                                .arg(newVersion.isEmpty() ? QStringLiteral("未解析") : newVersion)
                                .arg(currentPart.isEmpty() ? QStringLiteral("未知") : currentPart)
                                .arg(targetPart);

            if (!allowStart) {
                state += QStringLiteral("\n升级包版本不高于当前系统版本，按说明不执行升级");
            }
            m_firmwareStateLabel->setText(state);
        }
        m_firmwareStartBtn->setEnabled(allowStart);
    } else {
        if (m_firmwareStateLabel) {
            m_firmwareStateLabel->setText(QStringLiteral("未找到升级文件，请参考说明在U盘根目录放置.swu文件"));
        }
        m_firmwareStartBtn->setEnabled(false);
    }
}

void SystemSettingWindow::setupUI()
{
    auto *central = new QWidget(this);
    central->setObjectName("systemSettingCentral");
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    central->setStyleSheet(
        "QWidget#systemSettingWindow, QWidget{color:#eaf2ff;}"
        "QMainWindow{background:#0a1326;}"
        "QWidget#systemSettingCentral{background-image:url(:/images/inside_background.png);background-position:center;background-repeat:no-repeat;}"
    );

    QWidget *topBar = new QWidget(central);
    topBar->setFixedSize(1280, 82);
    topBar->setStyleSheet("background: url(:/images/topbar.png) no-repeat;");

    auto *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 12, 48, 48);
    homeBtn->setStyleSheet(
        "QPushButton { border:none; background-image:url(:/images/pict_home_up.png); background-repeat:no-repeat; }"
        "QPushButton:hover, QPushButton:pressed { background-image:url(:/images/pict_home_down.png); }"
    );
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this](){ emit requestReturnToMain(); close(); });

    auto *title = new QLabel(QStringLiteral("系统设置"), topBar);
    title->setGeometry(0, 0, 1280, 72);
    title->setStyleSheet("QLabel{color:#fff;font-size:36px;font-weight:700;background:transparent;}");
    title->setAlignment(Qt::AlignCenter);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *right = new QWidget(topBar);
    right->setGeometry(1280 - 16 - 280, 12, 280, 48);
    right->setStyleSheet("background:transparent;");
    auto *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(16);

    auto *btIcon = new QLabel();
    btIcon->setFixedSize(48, 48);
    btIcon->setPixmap(QPixmap(":/images/pict_bluetooth.png"));
    rightLay->addWidget(btIcon);

    auto *usbIcon = new QLabel();
    usbIcon->setFixedSize(48, 48);
    usbIcon->setPixmap(QPixmap(":/images/pict_usb.png"));
    rightLay->addWidget(usbIcon);

    auto *volIcon = new QLabel();
    volIcon->setFixedSize(48, 48);
    volIcon->setPixmap(QPixmap(":/images/pict_volume.png"));
    auto *volLabel = new QLabel(QStringLiteral("10"));
    volLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(volIcon);
    rightLay->addWidget(volLabel);

    auto *timeLabel = new QLabel(QTime::currentTime().toString("hh:mm"));
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(timeLabel);

    auto *content = new QFrame(central);
    content->setStyleSheet("QFrame{background:transparent;border:none;}");
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 24, 0, 0);
    contentLayout->setSpacing(0);

    m_subnavList = new QListWidget(content);
    m_subnavList->setFixedSize(280, 646);
    const QStringList subnavItems = {
        QStringLiteral("显示模式"),
        QStringLiteral("声音设置"),
        QStringLiteral("蓝牙设置"),
        QStringLiteral("系统信息"),
        QStringLiteral("恢复出厂"),
        QStringLiteral("系统升级")
    };
    for (const QString &item : subnavItems) {
        auto *it = new QListWidgetItem(item, m_subnavList);
        it->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    }
    m_subnavList->setStyleSheet(
        "QListWidget{border:none;background:rgba(0,0,0,0.22);font-size:32px;outline:none;color:#eaf2ff;}"
        "QListWidget::item{height:90px;padding:0;color:#eaf2ff;}"
        "QListWidget::item:selected{background-image:url(:/images/butt_subnav_on.png);background-repeat:no-repeat;background-position:center;color:#00faff;}"
        "QListWidget::item:hover{background-image:url(:/images/butt_subnav_on.png);background-repeat:no-repeat;background-position:center;color:#00faff;}"
    );
    m_subnavList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_subnavList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *pageHost = new QWidget(content);
    auto *pageHostLayout = new QVBoxLayout(pageHost);
    pageHostLayout->setContentsMargins(80, 0, 80, 0);
    pageHostLayout->setSpacing(0);

    m_pages = new QStackedWidget(pageHost);
    m_pages->setStyleSheet(
        "QStackedWidget{background:transparent;border:none;}"
        "QLabel{color:#e9f1ff;font-size:26px;}"
        "QGroupBox{border:1px solid rgba(0,104,255,0.55);margin-top:22px;padding-top:18px;font-size:26px;color:#d9e8ff;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 8px;}"
        "QSlider::groove:horizontal{height:8px;background:rgba(255,255,255,0.18);border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#00a5ff;border-radius:4px;}"
        "QSlider::handle:horizontal{width:18px;height:18px;margin:-5px 0;background:#00faff;border-radius:9px;}"
    );
    m_pages->addWidget(createDisplayPage());
    m_pages->addWidget(createSoundPage());
    m_pages->addWidget(createBluetoothPage());
    m_pages->addWidget(createInfoPage());
    m_pages->addWidget(createFactoryPage());
    m_pages->addWidget(createUpdatePage());
    pageHostLayout->addWidget(m_pages);

    contentLayout->addWidget(m_subnavList);
    contentLayout->addWidget(pageHost, 1);

    root->addWidget(topBar);
    root->addWidget(content, 1);

    connect(m_subnavList, &QListWidget::currentRowChanged, this, &SystemSettingWindow::onSubnavChanged);
    m_subnavList->setCurrentRow(0);
}

QWidget *SystemSettingWindow::createDisplayPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 24, 0, 0);
    layout->setSpacing(0);

    auto makeModeButton = [](const QString &text, bool active, const QString &radiusRule) {
        auto *btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setChecked(active);
        btn->setFixedSize(120, 44);
        btn->setStyleSheet(
            "QPushButton{border:none;background:#3f3d52;color:#fff;font-size:24px;}"
            "QPushButton:checked{background:#0452ca;}"
            "QPushButton:hover{background:#0452ca;}"
            + radiusRule
        );
        return btn;
    };

    auto *row1 = new QWidget(page);
    row1->setFixedHeight(172);
    row1->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.1);}");
    auto *row1Layout = new QHBoxLayout(row1);
    row1Layout->setContentsMargins(0, 0, 0, 0);
    row1Layout->setSpacing(16);
    auto *title1 = new QLabel(QStringLiteral("亮度模式"), row1);
    title1->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    title1->setFixedWidth(170);
    row1Layout->addWidget(title1);

    auto *right1 = new QWidget(row1);
    auto *right1Layout = new QVBoxLayout(right1);
    right1Layout->setContentsMargins(0, 0, 0, 0);
    right1Layout->setSpacing(16);

    auto *modeRow = new QWidget(right1);
    auto *modeRowLayout = new QHBoxLayout(modeRow);
    modeRowLayout->setContentsMargins(0, 0, 0, 0);
    modeRowLayout->setSpacing(2);
    auto *dayBtn = makeModeButton(QStringLiteral("白天"), true,
                                  "QPushButton{border-top-left-radius:22px;border-bottom-left-radius:22px;border-top-right-radius:0;border-bottom-right-radius:0;}");
    auto *nightBtn = makeModeButton(QStringLiteral("夜晚"), false, "QPushButton{border-radius:0;}");
    auto *autoBtn = makeModeButton(QStringLiteral("自动"), false,
                                   "QPushButton{border-top-left-radius:0;border-bottom-left-radius:0;border-top-right-radius:22px;border-bottom-right-radius:22px;}");
    auto *modeGroup = new QButtonGroup(page);
    modeGroup->setExclusive(true);
    modeGroup->addButton(dayBtn);
    modeGroup->addButton(nightBtn);
    modeGroup->addButton(autoBtn);
    modeRowLayout->addStretch();
    modeRowLayout->addWidget(dayBtn);
    modeRowLayout->addWidget(nightBtn);
    modeRowLayout->addWidget(autoBtn);

    auto *brightnessRow = new QWidget(right1);
    auto *brightnessLayout = new QHBoxLayout(brightnessRow);
    brightnessLayout->setContentsMargins(0, 0, 0, 0);
    brightnessLayout->setSpacing(10);
    auto *lowIcon = new QLabel(brightnessRow);
    lowIcon->setFixedSize(24, 24);
    lowIcon->setPixmap(QPixmap(":/images/pict_brightness_low.png").scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *slider = new QSlider(Qt::Horizontal, brightnessRow);
    slider->setRange(0, 100);
    slider->setValue(50);
    slider->setStyleSheet(
        "QSlider::groove:horizontal{height:8px;background:rgba(255,255,255,0.28);border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#00a5ff;border-radius:4px;}"
        "QSlider::handle:horizontal{background:url(:/images/pict_brightness_mark.png);width:24px;height:44px;margin:-18px 0;border:none;}"
    );
    auto *highIcon = new QLabel(brightnessRow);
    highIcon->setFixedSize(36, 36);
    highIcon->setPixmap(QPixmap(":/images/pict_brightness_high.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *tips = new QLabel(QStringLiteral("50"), brightnessRow);
    tips->setFixedSize(48, 38);
    tips->setAlignment(Qt::AlignCenter);
    tips->setStyleSheet("QLabel{background:url(:/images/pict_brightness_tips.png);font-size:24px;color:#fff;}");
    connect(slider, &QSlider::valueChanged, tips, [tips](int v) {
        tips->setText(QString::number(v));
    });
    brightnessLayout->addWidget(lowIcon);
    brightnessLayout->addWidget(slider, 1);
    brightnessLayout->addWidget(highIcon);
    brightnessLayout->addWidget(tips);

    right1Layout->addWidget(modeRow);
    right1Layout->addWidget(brightnessRow);
    row1Layout->addWidget(right1, 1);

    auto makeSwitchRow = [page](const QString &name, const QString &l, const QString &r) {
        auto *row = new QWidget(page);
        row->setFixedHeight(98);
        row->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.1);}");
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 24, 0, 24);
        h->setSpacing(16);
        auto *title = new QLabel(name, row);
        title->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
        title->setFixedWidth(170);
        h->addWidget(title);

        auto *btnBox = new QWidget(row);
        auto *btnLayout = new QHBoxLayout(btnBox);
        btnLayout->setContentsMargins(0, 0, 0, 0);
        btnLayout->setSpacing(0);
        auto *left = new QPushButton(l, btnBox);
        auto *right = new QPushButton(r, btnBox);
        left->setCheckable(true);
        right->setCheckable(true);
        left->setChecked(true);
        auto *group = new QButtonGroup(row);
        group->setExclusive(true);
        group->addButton(left);
        group->addButton(right);
        left->setFixedSize(120, 44);
        right->setFixedSize(120, 44);
        left->setStyleSheet(
            "QPushButton{border:none;background:url(:/images/butt_setting_choose_left.png);color:#fff;font-size:24px;}"
            "QPushButton:checked{color:#fff;}"
        );
        right->setStyleSheet(
            "QPushButton{border:none;background:url(:/images/butt_setting_choose_right.png);color:#fff;font-size:24px;}"
            "QPushButton:checked{color:#fff;}"
        );
        btnLayout->addStretch();
        btnLayout->addWidget(left);
        btnLayout->addWidget(right);
        h->addWidget(btnBox, 1);
        return row;
    };

    layout->addWidget(row1);
    layout->addWidget(makeSwitchRow(QStringLiteral("关屏时钟"), QStringLiteral("数字"), QStringLiteral("模拟")));
    layout->addWidget(makeSwitchRow(QStringLiteral("时钟制式"), QStringLiteral("12小时"), QStringLiteral("24小时")));
    layout->addStretch();
    return page;
}

QWidget *SystemSettingWindow::createSoundPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 24, 0, 0);
    layout->setSpacing(0);

    auto *volumeRow = new QWidget(page);
    volumeRow->setFixedHeight(122);
    volumeRow->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.1);}");
    auto *volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(0, 30, 0, 30);
    volumeLayout->setSpacing(16);
    auto *volumeTitle = new QLabel(QStringLiteral("音量"), volumeRow);
    volumeTitle->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    volumeTitle->setFixedWidth(170);
    volumeLayout->addWidget(volumeTitle);

    auto *vRight = new QWidget(volumeRow);
    auto *vRightLayout = new QHBoxLayout(vRight);
    vRightLayout->setContentsMargins(0, 0, 0, 0);
    vRightLayout->setSpacing(10);
    auto *lowIcon = new QLabel(vRight);
    lowIcon->setFixedSize(36, 36);
    lowIcon->setPixmap(QPixmap(":/images/pict_volume_low.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *slider = new QSlider(Qt::Horizontal, vRight);
    slider->setRange(0, 100);
    slider->setValue(50);
    slider->setStyleSheet(
        "QSlider::groove:horizontal{height:8px;background:rgba(255,255,255,0.28);border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#00a5ff;border-radius:4px;}"
        "QSlider::handle:horizontal{background:url(:/images/pict_brightness_mark.png);width:24px;height:44px;margin:-18px 0;border:none;}"
    );
    auto *highIcon = new QLabel(vRight);
    highIcon->setFixedSize(36, 36);
    highIcon->setPixmap(QPixmap(":/images/pict_volume_loud.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *tips = new QLabel(QStringLiteral("50"), vRight);
    tips->setFixedSize(48, 38);
    tips->setAlignment(Qt::AlignCenter);
    tips->setStyleSheet("QLabel{background:url(:/images/pict_brightness_tips.png);font-size:24px;color:#fff;}");
    connect(slider, &QSlider::valueChanged, tips, [tips](int v) {
        tips->setText(QString::number(v));
    });
    vRightLayout->addWidget(lowIcon);
    vRightLayout->addWidget(slider, 1);
    vRightLayout->addWidget(highIcon);
    vRightLayout->addWidget(tips);
    volumeLayout->addWidget(vRight, 1);

    auto *touchRow = new QWidget(page);
    touchRow->setFixedHeight(98);
    touchRow->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.1);}");
    auto *touchLayout = new QHBoxLayout(touchRow);
    touchLayout->setContentsMargins(0, 24, 0, 24);
    touchLayout->setSpacing(16);
    auto *touchTitle = new QLabel(QStringLiteral("屏幕点击音"), touchRow);
    touchTitle->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    touchTitle->setFixedWidth(170);
    touchLayout->addWidget(touchTitle);

    auto *tabWrap = new QWidget(touchRow);
    auto *tabLayout = new QHBoxLayout(tabWrap);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(2);
    const QStringList touchModes = {QStringLiteral("静音"), QStringLiteral("柔和"), QStringLiteral("响亮")};
    auto *touchGroup = new QButtonGroup(page);
    touchGroup->setExclusive(true);
    tabLayout->addStretch();
    for (int i = 0; i < touchModes.size(); ++i) {
        auto *btn = new QPushButton(touchModes[i], tabWrap);
        btn->setCheckable(true);
        btn->setChecked(i == 1);
        btn->setFixedSize(120, 44);
        btn->setStyleSheet(
            QString("QPushButton{border:none;background:#3f3d52;color:#fff;font-size:24px;%1}"
                    "QPushButton:checked{background:#0452ca;}"
                    "QPushButton:hover{background:#0452ca;}")
                .arg(i == 0 ? "border-radius:22px 0 0 22px;" : (i == touchModes.size() - 1 ? "border-radius:0 22px 22px 0;" : "border-radius:0;"))
        );
        touchGroup->addButton(btn);
        tabLayout->addWidget(btn);
    }
    touchLayout->addWidget(tabWrap, 1);

    auto *fieldRow = new QWidget(page);
    fieldRow->setFixedHeight(158);
    auto *fieldLayout = new QHBoxLayout(fieldRow);
    fieldLayout->setContentsMargins(0, 60, 0, 24);
    fieldLayout->setSpacing(16);
    auto *fieldTitle = new QLabel(QStringLiteral("声场模式"), fieldRow);
    fieldTitle->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    fieldTitle->setFixedWidth(170);
    fieldLayout->addWidget(fieldTitle);

    auto *fieldBtn = new QPushButton(QStringLiteral("低音增强"), fieldRow);
    fieldBtn->setFixedSize(168, 44);
    fieldBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:22px;background:#0452ca;color:#fff;font-size:24px;padding:0 18px;}"
        "QPushButton:hover{background:#00faff;color:#08324d;}"
    );
    connect(fieldBtn, &QPushButton::clicked, this, [this, fieldBtn]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("声场模式"));
        dialog.setFixedSize(696, 480);
        dialog.setStyleSheet("QDialog{background:#08142d;color:#fff;border:none;}");

        auto *dLayout = new QVBoxLayout(&dialog);
        dLayout->setContentsMargins(0, 150, 0, 0);
        dLayout->setSpacing(0);
        auto *list = new QListWidget(&dialog);
        list->setStyleSheet(
            "QListWidget{background:transparent;border:none;outline:none;font-size:35px;color:#fff;}"
            "QListWidget::item{height:78px;border:1px solid #0068ff;background:transparent;text-align:center;}"
            "QListWidget::item:selected{border:2px solid #00faff;color:#00faff;}"
            "QListWidget::item:hover{border:2px solid #00faff;color:#00faff;}"
        );
        list->setFlow(QListView::LeftToRight);
        list->setWrapping(true);
        list->setResizeMode(QListView::Adjust);
        list->setGridSize(QSize(200, 82));
        list->setSpacing(8);
        list->setFixedSize(696, 330);
        const QStringList modes = {
            QStringLiteral("立体声"), QStringLiteral("环绕声"), QStringLiteral("低音增强"),
            QStringLiteral("高音增强"), QStringLiteral("平衡音"), QStringLiteral("音场定位"),
            QStringLiteral("降噪音效"), QStringLiteral("虚拟声场"), QStringLiteral("响度补偿")
        };
        list->addItems(modes);
        const int idx = modes.indexOf(fieldBtn->text());
        if (idx >= 0) {
            list->setCurrentRow(idx);
        }
        dLayout->addWidget(list, 0, Qt::AlignHCenter);

        connect(list, &QListWidget::itemClicked, &dialog, [&dialog, fieldBtn](QListWidgetItem *item) {
            if (item) {
                fieldBtn->setText(item->text());
                dialog.accept();
            }
        });
        dialog.exec();
    });
    fieldLayout->addWidget(fieldBtn);
    fieldLayout->addStretch();

    layout->addWidget(volumeRow);
    layout->addWidget(touchRow);
    layout->addWidget(fieldRow);
    layout->addStretch();
    return page;
}

QWidget *SystemSettingWindow::createBluetoothPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 24, 0, 0);
    layout->setSpacing(0);

    auto *tabs = new QTabWidget(page);
    tabs->setFixedWidth(648);
    tabs->tabBar()->setUsesScrollButtons(false);
    tabs->setStyleSheet(
        "QTabWidget::pane{border:none;background:transparent;}"
        "QTabBar::tab{width:160px;height:66px;border:none;color:#fff;font-size:28px;padding:0;margin-right:1px;}"
        "QTabBar::tab:first{background:url(:/images/butt_tab_left_down.png) no-repeat center center;}"
        "QTabBar::tab:middle{background:url(:/images/butt_tab_center_down.png) no-repeat center center;}"
        "QTabBar::tab:last{background:url(:/images/butt_tab_right_down.png) no-repeat center center;margin-right:0;}"
        "QTabBar::tab:selected:first{background:url(:/images/butt_tab_left_on.png) no-repeat center center;color:#00faff;}"
        "QTabBar::tab:selected:middle{background:url(:/images/butt_tab_center_on.png) no-repeat center center;color:#00faff;}"
        "QTabBar::tab:selected:last{background:url(:/images/butt_tab_right_on.png) no-repeat center center;color:#00faff;}"
    );

    auto makeBlueBtn = [](const QString &text, int w = 112) {
        auto *btn = new QPushButton(text);
        btn->setFixedSize(w, 54);
        btn->setStyleSheet(
            "QPushButton{background:none;border:2px solid #0068ff;color:#fff;font-size:24px;}"
            "QPushButton:hover{border-color:#00faff;color:#00faff;}"
        );
        return btn;
    };

    auto *openTab = new QWidget();
    auto *openLayout = new QVBoxLayout(openTab);
    openLayout->setContentsMargins(0, 0, 0, 0);
    openLayout->setSpacing(24);

    auto *switchRow = new QWidget(openTab);
    switchRow->setFixedHeight(110);
    auto *switchRowLayout = new QHBoxLayout(switchRow);
    switchRowLayout->setContentsMargins(0, 24, 0, 24);
    switchRowLayout->setSpacing(16);
    auto *switchLabel = new QLabel(QStringLiteral("蓝牙"), switchRow);
    switchLabel->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    switchLabel->setFixedWidth(170);
    auto *enableBtn = new QPushButton(switchRow);
    enableBtn->setCheckable(true);
    enableBtn->setChecked(true);
    enableBtn->setFixedSize(88, 44);
    enableBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_setting_close.png) no-repeat center;}"
        "QPushButton:checked{background:url(:/images/butt_setting_open.png) no-repeat center;}"
    );
    switchRowLayout->addWidget(switchLabel);
    switchRowLayout->addWidget(enableBtn);
    switchRowLayout->addStretch();

    auto *intro = new QLabel(QStringLiteral("在移动设备上启动蓝牙，并搜索本设备name：wodelanya\n匹配密码：0000"), openTab);
    intro->setStyleSheet("QLabel{font-size:28px;line-height:42px;color:#eaf2ff;}");
    intro->setWordWrap(true);

    auto *renameBtn = makeBlueBtn(QStringLiteral("修改名称"), 168);
    auto *pwdBtn = makeBlueBtn(QStringLiteral("修改密码"), 168);
    auto *openBtnRow = new QHBoxLayout();
    openBtnRow->addWidget(renameBtn);
    openBtnRow->addWidget(pwdBtn);
    openBtnRow->addStretch();

    auto *deviceName = new QString(QStringLiteral("wodelanya"));
    auto *pairPwd = new QString(QStringLiteral("0000"));
    connect(renameBtn, &QPushButton::clicked, this, [this, intro, deviceName, pairPwd]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("蓝牙名称"));
        dialog.setFixedSize(680, 220);
        dialog.setStyleSheet("QDialog{background:#0d1f3f;color:#fff;border:1px solid #0068ff;}");
        auto *dLayout = new QVBoxLayout(&dialog);
        auto *line = new QLineEdit(*deviceName, &dialog);
        line->setStyleSheet("QLineEdit{height:54px;background:rgba(255,255,255,0.08);border:1px solid #0068ff;color:#fff;font-size:28px;padding-left:16px;}");
        auto *ok = new QPushButton(QStringLiteral("确认"), &dialog);
        ok->setFixedSize(168, 54);
        ok->setStyleSheet("QPushButton{background:#0068ff;color:#fff;border:none;font-size:26px;}QPushButton:hover{background:#00a5ff;}");
        dLayout->addWidget(line);
        auto *row = new QHBoxLayout();
        row->addStretch();
        row->addWidget(ok);
        row->addStretch();
        dLayout->addLayout(row);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() == QDialog::Accepted && !line->text().trimmed().isEmpty()) {
            *deviceName = line->text().trimmed();
            intro->setText(QStringLiteral("在移动设备上启动蓝牙，并搜索本设备name：%1\n匹配密码：%2").arg(*deviceName, *pairPwd));
        }
    });
    connect(pwdBtn, &QPushButton::clicked, this, [this, intro, deviceName, pairPwd]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("密码设置"));
        dialog.setFixedSize(680, 220);
        dialog.setStyleSheet("QDialog{background:#0d1f3f;color:#fff;border:1px solid #0068ff;}");
        auto *dLayout = new QVBoxLayout(&dialog);
        auto *line = new QLineEdit(*pairPwd, &dialog);
        line->setEchoMode(QLineEdit::Password);
        line->setStyleSheet("QLineEdit{height:54px;background:rgba(255,255,255,0.08);border:1px solid #0068ff;color:#fff;font-size:28px;padding-left:16px;}");
        auto *ok = new QPushButton(QStringLiteral("确认"), &dialog);
        ok->setFixedSize(168, 54);
        ok->setStyleSheet("QPushButton{background:#0068ff;color:#fff;border:none;font-size:26px;}QPushButton:hover{background:#00a5ff;}");
        dLayout->addWidget(line);
        auto *row = new QHBoxLayout();
        row->addStretch();
        row->addWidget(ok);
        row->addStretch();
        dLayout->addLayout(row);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() == QDialog::Accepted && !line->text().trimmed().isEmpty()) {
            *pairPwd = line->text().trimmed();
            intro->setText(QStringLiteral("在移动设备上启动蓝牙，并搜索本设备name：%1\n匹配密码：%2").arg(*deviceName, *pairPwd));
        }
    });

    openLayout->addWidget(switchRow);
    openLayout->addWidget(intro);
    openLayout->addLayout(openBtnRow);
    openLayout->addStretch();

    auto *pairTab = new QWidget();
    auto *pairLayout = new QVBoxLayout(pairTab);
    pairLayout->setContentsMargins(0, 0, 0, 0);
    pairLayout->setSpacing(12);
    auto *currentLabel = new QLabel(QStringLiteral("当前连接的设备"), pairTab);
    currentLabel->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    auto *currentDevice = new QLabel(QStringLiteral("手机：13723467654"), pairTab);
    currentDevice->setStyleSheet("QLabel{height:60px;border:1px solid #0068ff;background:rgba(255,255,255,0.10);font-size:28px;padding-left:18px;color:#fff;}");
    auto *chooseLabel = new QLabel(QStringLiteral("请选择需要连接的手机"), pairTab);
    chooseLabel->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;margin-top:8px;}");
    auto *pairList = new QListWidget(pairTab);
    pairList->addItems({QStringLiteral("手机：13723467654"), QStringLiteral("手机：13723467655"), QStringLiteral("手机：13723467656")});
    pairList->setCurrentRow(0);
    pairList->setFixedHeight(192);
    pairList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;font-size:28px;color:#fff;}"
        "QListWidget::item{height:60px;border:1px solid #0068ff;background:rgba(255,255,255,0.10);margin-bottom:12px;padding-left:16px;}"
        "QListWidget::item:selected{border:2px solid #00faff;color:#00faff;}"
        "QListWidget::item:hover{border:2px solid #00faff;color:#00faff;}"
    );
    auto *pairBtnRow = new QHBoxLayout();
    auto *connectBtn = makeBlueBtn(QStringLiteral("连接"));
    auto *disconnectBtn = makeBlueBtn(QStringLiteral("断开"));
    pairBtnRow->addStretch();
    pairBtnRow->addWidget(connectBtn);
    pairBtnRow->addSpacing(28);
    pairBtnRow->addWidget(disconnectBtn);
    pairBtnRow->addStretch();
    connect(connectBtn, &QPushButton::clicked, this, [pairList, currentDevice]() {
        if (pairList->currentItem()) {
            currentDevice->setText(pairList->currentItem()->text());
        }
    });
    connect(disconnectBtn, &QPushButton::clicked, this, [currentDevice]() {
        currentDevice->setText(QStringLiteral("未连接"));
    });

    pairLayout->addWidget(currentLabel);
    pairLayout->addWidget(currentDevice);
    pairLayout->addWidget(chooseLabel);
    pairLayout->addWidget(pairList);
    pairLayout->addLayout(pairBtnRow);
    pairLayout->addStretch();

    auto *removeTab = new QWidget();
    auto *removeLayout = new QVBoxLayout(removeTab);
    removeLayout->setContentsMargins(0, 0, 0, 0);
    removeLayout->setSpacing(12);
    auto *removeTitle = new QLabel(QStringLiteral("请选择需要连接的手机"), removeTab);
    removeTitle->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    auto *removeList = new QListWidget(removeTab);
    removeList->addItems({QStringLiteral("手机：13723467654"), QStringLiteral("手机：13723467655"), QStringLiteral("手机：13723467656")});
    removeList->setCurrentRow(0);
    removeList->setFixedHeight(230);
    removeList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;font-size:28px;color:#fff;}"
        "QListWidget::item{height:60px;border:1px solid #0068ff;background:rgba(255,255,255,0.10);margin-bottom:12px;padding-left:16px;}"
        "QListWidget::item:selected{border:2px solid #00faff;color:#00faff;}"
        "QListWidget::item:hover{border:2px solid #00faff;color:#00faff;}"
    );
    auto *removeBtn = makeBlueBtn(QStringLiteral("确认"));
    auto *removeBtnRow = new QHBoxLayout();
    removeBtnRow->addStretch();
    removeBtnRow->addWidget(removeBtn);
    removeBtnRow->addStretch();
    connect(removeBtn, &QPushButton::clicked, this, [removeList]() {
        if (removeList->currentRow() >= 0) {
            delete removeList->takeItem(removeList->currentRow());
        }
    });
    removeLayout->addWidget(removeTitle);
    removeLayout->addWidget(removeList);
    removeLayout->addLayout(removeBtnRow);
    removeLayout->addStretch();

    auto *passwordTab = new QWidget();
    auto *passwordLayout = new QVBoxLayout(passwordTab);
    passwordLayout->setContentsMargins(0, 0, 0, 0);
    passwordLayout->setSpacing(8);
    auto *pwdEdit = new QLineEdit(QStringLiteral("1234"), passwordTab);
    pwdEdit->setReadOnly(true);
    pwdEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pwdEdit->setStyleSheet("QLineEdit{height:62px;background:rgba(255,255,255,0.08);border:1px solid #0068ff;color:#fff;font-size:34px;padding-left:24px;}");
    auto *keypad = new QWidget(passwordTab);
    auto *grid = new QGridLayout(keypad);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    const QStringList keys = {"1","2","3","4","5","6","7","8","9","*","0","#"};
    for (int i = 0; i < keys.size(); ++i) {
        auto *btn = new QPushButton(keys[i], keypad);
        btn->setFixedSize(120, 86);
        btn->setStyleSheet("QPushButton{border:1px solid #0068ff;background:rgba(255,255,255,0.08);color:#fff;font-size:36px;}QPushButton:hover{border-color:#00faff;color:#00faff;}");
        grid->addWidget(btn, i / 3, i % 3);
        connect(btn, &QPushButton::clicked, this, [pwdEdit, btn]() {
            pwdEdit->setText((pwdEdit->text() + btn->text()).right(8));
        });
    }
    auto *btnRow = new QHBoxLayout();
    auto *backspace = makeBlueBtn(QStringLiteral("删除"));
    auto *confirm = makeBlueBtn(QStringLiteral("确认"));
    btnRow->addStretch();
    btnRow->addWidget(backspace);
    btnRow->addSpacing(28);
    btnRow->addWidget(confirm);
    btnRow->addStretch();
    connect(backspace, &QPushButton::clicked, this, [pwdEdit]() {
        pwdEdit->setText(pwdEdit->text().left(qMax(0, pwdEdit->text().size() - 1)));
    });
    connect(confirm, &QPushButton::clicked, this, [pairPwd, pwdEdit, intro, deviceName]() {
        if (!pwdEdit->text().isEmpty()) {
            *pairPwd = pwdEdit->text();
            intro->setText(QStringLiteral("在移动设备上启动蓝牙，并搜索本设备name：%1\n匹配密码：%2").arg(*deviceName, *pairPwd));
        }
    });
    passwordLayout->addWidget(pwdEdit);
    passwordLayout->addWidget(keypad, 0, Qt::AlignHCenter);
    passwordLayout->addLayout(btnRow);
    passwordLayout->addStretch();

    tabs->addTab(openTab, QStringLiteral("蓝牙开启"));
    tabs->addTab(pairTab, QStringLiteral("匹配设备"));
    tabs->addTab(removeTab, QStringLiteral("删除设备"));
    tabs->addTab(passwordTab, QStringLiteral("密码设置"));

    layout->addWidget(tabs, 0, Qt::AlignLeft);
    return page;
}

QWidget *SystemSettingWindow::createInfoPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *listWrap = new QWidget(page);
    listWrap->setFixedWidth(630);
    auto *listLayout = new QVBoxLayout(listWrap);
    listLayout->setContentsMargins(0, 110, 0, 0);
    listLayout->setSpacing(24);

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("软件版本："), QStringLiteral("V3.2.1")},
        {QStringLiteral("硬件型号："), QStringLiteral("CH-2004")},
        {QStringLiteral("系统更新日期："), QStringLiteral("2026-1-26")}
    };

    for (const auto &row : rows) {
        auto *item = new QFrame(listWrap);
        item->setFixedHeight(60);
        item->setStyleSheet(
            "QFrame{border:1px solid #0068FF;background:rgba(255,255,255,0.10);}"
            "QLabel{border:none;background:transparent;color:#eaf2ff;font-size:28px;}"
        );

        auto *itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(24, 0, 24, 0);
        itemLayout->setSpacing(0);

        auto *key = new QLabel(row.first, item);
        key->setFixedWidth(260);
        auto *value = new QLabel(row.second, item);

        itemLayout->addWidget(key);
        itemLayout->addWidget(value, 1);
        listLayout->addWidget(item);
    }

    auto *centerLayout = new QHBoxLayout();
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addStretch();
    centerLayout->addWidget(listWrap);
    centerLayout->addStretch();

    layout->addLayout(centerLayout);
    layout->addStretch();
    return page;
}

QWidget *SystemSettingWindow::createFactoryPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 138, 0, 0);
    layout->setSpacing(55);

    auto *warn = new QLabel(QStringLiteral("恢复出厂设置将清除所有个人数据，\n请谨慎操作"), page);
    warn->setAlignment(Qt::AlignCenter);
    warn->setStyleSheet("QLabel{font-size:32px;line-height:48px;color:#fff;background:transparent;}");

    auto *resetBtn = new QPushButton(QStringLiteral("恢复出厂设置"), page);
    resetBtn->setFixedSize(192, 54);
    resetBtn->setStyleSheet(
        "QPushButton{background:none;border:2px solid #0068ff;color:#fff;font-size:24px;}"
        "QPushButton:hover{border-color:#00faff;color:#00faff;}"
    );

    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("出厂设置"));
        dialog.setFixedSize(530, 272);
        dialog.setStyleSheet("QDialog{border:1px solid #0068ff;background:rgba(26,41,80,0.95);border-radius:25px;}QLabel{color:#fff;}");

        auto *root = new QVBoxLayout(&dialog);
        root->setContentsMargins(28, 28, 28, 28);
        root->setSpacing(12);

        auto *top = new QLabel(QStringLiteral("您确定执行此操作吗"), &dialog);
        top->setMinimumHeight(104);
        top->setStyleSheet("QLabel{font-size:36px;padding-left:124px;background:url(:/images/pict_popalert_icon.png) left top no-repeat;}");

        auto *row = new QHBoxLayout();
        row->setSpacing(56);
        auto *ok = new QPushButton(QStringLiteral("确认"), &dialog);
        auto *cancel = new QPushButton(QStringLiteral("取消"), &dialog);
        ok->setFixedSize(168, 64);
        cancel->setFixedSize(168, 64);
        ok->setStyleSheet("QPushButton{background:none;border:2px solid #0068ff;color:#fff;font-size:36px;}QPushButton:hover{border-color:#00faff;color:#00faff;}");
        cancel->setStyleSheet("QPushButton{background:none;border:2px solid #999;color:#fff;font-size:36px;}QPushButton:hover{border-color:#ccc;}");
        row->addWidget(ok);
        row->addWidget(cancel);

        root->addWidget(top);
        root->addLayout(row);

        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        dialog.exec();
    });

    layout->addWidget(warn);
    layout->addWidget(resetBtn, 0, Qt::AlignHCenter);
    layout->addStretch();
    return page;
}

QWidget *SystemSettingWindow::createUpdatePage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_updateTabWidget = new QTabWidget(page);
    m_updateTabWidget->setFixedWidth(1000);
    m_updateTabWidget->tabBar()->setUsesScrollButtons(false);
    m_updateTabWidget->setStyleSheet(
        "QTabWidget::tab-bar{alignment:center;}"
        "QTabWidget::pane{border:none;background:transparent;}"
        "QTabBar::tab{width:160px;height:66px;border:none;color:#fff;font-size:28px;padding:0;}"
        "QTabBar::tab:first{background:url(:/images/butt_tab_left_on.png) no-repeat center center;}"
        "QTabBar::tab:last{background:url(:/images/butt_tab_right_down.png) no-repeat center center;}"
        "QTabBar::tab:selected:first{background:url(:/images/butt_tab_left_on.png) no-repeat center center;color:#fff;}"
        "QTabBar::tab:selected:last{background:url(:/images/butt_tab_right_on.png) no-repeat center center;color:#fff;}"
        "QTabBar::tab:!selected:first{background:url(:/images/butt_tab_left_down.png) no-repeat center center;}"
        "QTabBar::tab:!selected:last{background:url(:/images/butt_tab_right_down.png) no-repeat center center;}"
    );

    auto *appUpdateWidget = new QWidget();
    auto *appUpdateLayout = new QVBoxLayout(appUpdateWidget);
    appUpdateLayout->setContentsMargins(0, 56, 0, 0);
    appUpdateLayout->setSpacing(18);

    m_updateIntroLabel = new QLabel(QStringLiteral("通过U盘对系统进行升级更新，\n更新过程请勿进行其他操作！"), appUpdateWidget);
    m_updateIntroLabel->setAlignment(Qt::AlignCenter);
    m_updateIntroLabel->setWordWrap(true);
    m_updateIntroLabel->setFixedWidth(1000);
    m_updateIntroLabel->setStyleSheet("QLabel{font-size:32px;line-height:48px;color:#f0f6ff;}");

    m_updateStateLabel = new QLabel(QStringLiteral("等待升级操作"), appUpdateWidget);
    m_updateStateLabel->setAlignment(Qt::AlignCenter);
    m_updateStateLabel->setWordWrap(true);
    m_updateStateLabel->setFixedWidth(1000);
    m_updateStateLabel->setStyleSheet("QLabel{font-size:32px;line-height:48px;color:#fff;}");
    m_updateStateLabel->hide();

    m_updateProgressBar = new QProgressBar(appUpdateWidget);
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setFixedWidth(430);
    m_updateProgressBar->setStyleSheet(
        "QProgressBar{border:none;background:rgba(255,255,255,0.15);height:10px;border-radius:5px;}"
        "QProgressBar::chunk{background:#00a5ff;border-radius:5px;}"
    );

    m_updateProgressText = new QLabel(QStringLiteral("0%"), appUpdateWidget);
    m_updateProgressText->setStyleSheet("QLabel{font-size:24px;color:#ffffff;}");

    auto *progressTitle = new QLabel(QStringLiteral("进度："), appUpdateWidget);
    progressTitle->setStyleSheet("QLabel{font-size:24px;color:#d8e8ff;}");

    m_updateProgressRowWidget = new QWidget(appUpdateWidget);
    auto *progressRow = new QHBoxLayout(m_updateProgressRowWidget);
    progressRow->setContentsMargins(0, 0, 0, 0);
    progressRow->addStretch();
    progressRow->addWidget(progressTitle);
    progressRow->addWidget(m_updateProgressBar);
    progressRow->addWidget(m_updateProgressText);
    progressRow->addStretch();
    m_updateProgressRowWidget->hide();

    auto *startRow = new QHBoxLayout();
    m_updateStartBtn = new QPushButton(QStringLiteral("启动更新"), appUpdateWidget);
    m_updateCancelBtn = new QPushButton(QStringLiteral("取消更新"), appUpdateWidget);
    m_updateStartBtn->setFixedSize(192, 54);
    m_updateCancelBtn->setFixedSize(192, 54);
    m_updateStartBtn->setStyleSheet("QPushButton{background:transparent;color:#ffffff;border:2px solid #0068ff;font-size:24px;}"
                                    "QPushButton:hover{border-color:#00faff;color:#00faff;}");
    m_updateCancelBtn->setStyleSheet("QPushButton{background:transparent;color:#ffffff;border:2px solid #0068ff;font-size:24px;}"
                                     "QPushButton:hover{border-color:#00faff;color:#00faff;}");
    connect(m_updateStartBtn, &QPushButton::clicked, this, &SystemSettingWindow::onStartUpdate);
    connect(m_updateCancelBtn, &QPushButton::clicked, this, &SystemSettingWindow::onCancelUpdate);

    startRow->addStretch();
    startRow->addWidget(m_updateStartBtn);
    startRow->addStretch();

    auto *cancelRow = new QHBoxLayout();
    cancelRow->addStretch();
    cancelRow->addWidget(m_updateCancelBtn);
    cancelRow->addStretch();
    m_updateCancelBtn->hide();

    appUpdateLayout->addStretch();
    appUpdateLayout->addWidget(m_updateIntroLabel);
    appUpdateLayout->addWidget(m_updateStateLabel);
    appUpdateLayout->addWidget(m_updateProgressRowWidget);
    appUpdateLayout->addStretch();
    appUpdateLayout->addLayout(startRow);
    appUpdateLayout->addLayout(cancelRow);
    appUpdateLayout->addStretch();

    auto *firmwareUpdateWidget = new QWidget();
    auto *firmwareUpdateLayout = new QVBoxLayout(firmwareUpdateWidget);
    firmwareUpdateLayout->setContentsMargins(0, 56, 0, 0);
    firmwareUpdateLayout->setSpacing(18);

    m_firmwareIntroLabel = new QLabel(QStringLiteral("通过U盘对系统进行固件升级更新，\n更新过程请勿进行其他操作！"), firmwareUpdateWidget);
    m_firmwareIntroLabel->setAlignment(Qt::AlignCenter);
    m_firmwareIntroLabel->setWordWrap(true);
    m_firmwareIntroLabel->setFixedWidth(1000);
    m_firmwareIntroLabel->setStyleSheet("QLabel{font-size:32px;line-height:48px;color:#f0f6ff;}");

    m_firmwareStateLabel = new QLabel(QStringLiteral("正在更新..."), firmwareUpdateWidget);
    m_firmwareStateLabel->setAlignment(Qt::AlignCenter);
    m_firmwareStateLabel->setWordWrap(true);
    m_firmwareStateLabel->setFixedWidth(1000);
    m_firmwareStateLabel->setStyleSheet("QLabel{font-size:32px;line-height:48px;color:#fff;}");
    m_firmwareStateLabel->hide();

    m_firmwareProgressBar = new QProgressBar(firmwareUpdateWidget);
    m_firmwareProgressBar->setRange(0, 100);
    m_firmwareProgressBar->setValue(0);
    m_firmwareProgressBar->setFixedWidth(430);
    m_firmwareProgressBar->setStyleSheet(
        "QProgressBar{border:none;background:rgba(255,255,255,0.15);height:10px;border-radius:5px;}"
        "QProgressBar::chunk{background:#00a5ff;border-radius:5px;}"
    );

    m_firmwareProgressText = new QLabel(QStringLiteral("0%"), firmwareUpdateWidget);
    m_firmwareProgressText->setStyleSheet("QLabel{font-size:24px;color:#ffffff;}");

    auto *fwProgressTitle = new QLabel(QStringLiteral("进度："), firmwareUpdateWidget);
    fwProgressTitle->setStyleSheet("QLabel{font-size:24px;color:#d8e8ff;}");

    m_firmwareProgressRowWidget = new QWidget(firmwareUpdateWidget);
    auto *fwProgressRow = new QHBoxLayout(m_firmwareProgressRowWidget);
    fwProgressRow->setContentsMargins(0, 0, 0, 0);
    fwProgressRow->addStretch();
    fwProgressRow->addWidget(fwProgressTitle);
    fwProgressRow->addWidget(m_firmwareProgressBar);
    fwProgressRow->addWidget(m_firmwareProgressText);
    fwProgressRow->addStretch();
    m_firmwareProgressRowWidget->hide();

    auto *fwStartRow = new QHBoxLayout();
    m_firmwareStartBtn = new QPushButton(QStringLiteral("启动更新"), firmwareUpdateWidget);
    m_firmwareStartBtn->setFixedSize(192, 54);
    m_firmwareStartBtn->setStyleSheet("QPushButton{background:transparent;color:#ffffff;border:2px solid #0068ff;font-size:24px;}"
                                      "QPushButton:hover{border-color:#00faff;color:#00faff;}");
    connect(m_firmwareStartBtn, &QPushButton::clicked, this, [this]() {
        QString updateFile;
        if (!m_otaManager->checkUpdateFile(updateFile)) {
            if (m_firmwareStateLabel) {
                m_firmwareStateLabel->setText(QStringLiteral("未找到升级文件，请将.swu文件复制到U盘根目录"));
                m_firmwareStateLabel->show();
            }
            if (m_firmwareIntroLabel) {
                m_firmwareIntroLabel->hide();
            }
            return;
        }
        const QString newVersion = m_otaManager->parseVersionFromSWU(updateFile).trimmed();
        m_checkedUpdateFile = updateFile;
        m_checkedNewVersion = newVersion;
        m_otaManager->startUpdate(updateFile, newVersion);
    });
    fwStartRow->addStretch();
    fwStartRow->addWidget(m_firmwareStartBtn);
    fwStartRow->addStretch();

    auto *fwCancelRow = new QHBoxLayout();
    m_firmwareCancelBtn = new QPushButton(QStringLiteral("取消更新"), firmwareUpdateWidget);
    m_firmwareCancelBtn->setFixedSize(192, 54);
    m_firmwareCancelBtn->setStyleSheet("QPushButton{background:transparent;color:#ffffff;border:2px solid #0068ff;font-size:24px;}"
                                       "QPushButton:hover{border-color:#00faff;color:#00faff;}");
    connect(m_firmwareCancelBtn, &QPushButton::clicked, this, [this]() {
        m_otaManager->cancelUpdate();
    });
    fwCancelRow->addStretch();
    fwCancelRow->addWidget(m_firmwareCancelBtn);
    fwCancelRow->addStretch();
    m_firmwareCancelBtn->hide();

    firmwareUpdateLayout->addStretch();
    firmwareUpdateLayout->addWidget(m_firmwareIntroLabel);
    firmwareUpdateLayout->addWidget(m_firmwareStateLabel);
    firmwareUpdateLayout->addWidget(m_firmwareProgressRowWidget);
    firmwareUpdateLayout->addStretch();
    firmwareUpdateLayout->addLayout(fwStartRow);
    firmwareUpdateLayout->addLayout(fwCancelRow);
    firmwareUpdateLayout->addStretch();

    m_updateTabWidget->addTab(appUpdateWidget, QStringLiteral("应用更新"));
    m_updateTabWidget->addTab(firmwareUpdateWidget, QStringLiteral("固件更新"));

    layout->addWidget(m_updateTabWidget, 0, Qt::AlignHCenter);
    return page;
}
