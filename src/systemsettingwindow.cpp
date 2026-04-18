#include "systemsettingwindow.h"
#include "otamanager.h"
#include "devicedetect.h"
#include "topbarwidget.h"
#include "backlight.h"
#include "appsignals.h"
#include "t507sdkbridge.h"

#include <QButtonGroup>
#include <QKeyEvent>
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
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>
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

static void performFactoryReset()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    qDebug() << "SystemSettingWindow: factory reset cleared application settings.";
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

    auto *topBarRight = new TopBarRightWidget(topBar);
    topBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                             TopBarRightWidget::preferredWidth(), 48);

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
            "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;border:none;box-shadow:none;}"
            + radiusRule
        );
        return btn;
    };

    auto makeDivider = [page]() {
        auto *div = new QFrame(page);
        div->setFixedHeight(2);
        div->setStyleSheet("QFrame{background:rgba(255,255,255,0.1);border:none;}");
        return div;
    };

    auto *row1 = new QWidget(page);
    row1->setFixedHeight(172);
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
    const int initSliderVal = Backlight::backlightToSlider(Backlight::get());
    auto *slider = new QSlider(Qt::Horizontal, brightnessRow);
    slider->setRange(0, 100);
    slider->setValue(initSliderVal);
    slider->setFixedHeight(44);
    slider->setStyleSheet(
        "QSlider::groove:horizontal{height:8px;background:rgba(255,255,255,0.28);border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#00a5ff;border-radius:4px;}"
        "QSlider::handle:horizontal{background:url(:/images/pict_brightness_mark.png);width:24px;height:44px;margin:-18px 0;border:none;}"
    );
    auto *highIcon = new QLabel(brightnessRow);
    highIcon->setFixedSize(36, 36);
    highIcon->setPixmap(QPixmap(":/images/pict_brightness_high.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *tips = new QLabel(QString::number(initSliderVal), brightnessRow);
    tips->setFixedSize(48, 38);
    tips->setAlignment(Qt::AlignCenter);
    tips->setStyleSheet("QLabel{background:url(:/images/pict_brightness_tips.png);font-size:24px;color:#fff;}");
    connect(slider, &QSlider::valueChanged, tips, [tips](int v) {
        tips->setText(QString::number(v));
        Backlight::set(Backlight::sliderToBacklight(v));
    });
    // 亮度模式预设（slider 0–100）：白天=100 / 夜晚=20 / 自动=打开时实测值
    const int kDaySlider   = 100;
    const int kNightSlider = 20;
    const int kAutoSlider  = initSliderVal;
    connect(dayBtn,   &QPushButton::toggled, slider, [slider, kDaySlider  ](bool on){ if (on) slider->setValue(kDaySlider);   });
    connect(nightBtn, &QPushButton::toggled, slider, [slider, kNightSlider](bool on){ if (on) slider->setValue(kNightSlider); });
    connect(autoBtn,  &QPushButton::toggled, slider, [slider, kAutoSlider ](bool on){ if (on) slider->setValue(kAutoSlider);  });

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
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 27, 0, 27);
        h->setSpacing(16);
        auto *title = new QLabel(name, row);
        title->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
        title->setFixedWidth(170);
        h->addWidget(title);
        h->addStretch();

        // CSS .setting_btn: 整体 240×44 图片作容器背景，点选哪侧换图
        auto *container = new QWidget(row);
        container->setFixedSize(240, 44);
        container->setStyleSheet("QWidget{background:url(:/images/butt_setting_choose_left.png) no-repeat;}");

        auto *leftBtn  = new QPushButton(l,  container);
        auto *rightBtn = new QPushButton(r, container);
        leftBtn->setGeometry(0,   0, 120, 44);
        rightBtn->setGeometry(120, 0, 120, 44);
        const QString btnBase =
            "QPushButton{border:none;background:transparent;color:#fff;font-size:24px;}"
            "QPushButton:hover{color:#00faff;}"
            "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;border:none;box-shadow:none;}";
        leftBtn->setStyleSheet(btnBase);
        rightBtn->setStyleSheet(btnBase);
        leftBtn->setCursor(Qt::PointingHandCursor);
        rightBtn->setCursor(Qt::PointingHandCursor);

        // 切换：点左 → butt_setting_choose_left.png；点右 → butt_setting_choose_right.png
        QObject::connect(leftBtn,  &QPushButton::clicked, container, [container](){
            container->setStyleSheet("QWidget{background:url(:/images/butt_setting_choose_left.png) no-repeat;}");
        });
        QObject::connect(rightBtn, &QPushButton::clicked, container, [container](){
            container->setStyleSheet("QWidget{background:url(:/images/butt_setting_choose_right.png) no-repeat;}");
        });

        h->addWidget(container);
        return row;
    };

    layout->addWidget(row1);
    layout->addWidget(makeDivider());
    layout->addWidget(makeSwitchRow(QStringLiteral("关屏时钟"), QStringLiteral("数字"), QStringLiteral("模拟")));
    layout->addWidget(makeDivider());

    // 时钟制式行（单独实现以便连接 AppSignals::clockFormatChanged）
    {
        const bool init24h = qApp->property("appClock24h").toBool();
        auto *clockRow = new QWidget(page);
        clockRow->setFixedHeight(98);
        clockRow->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.1);}");
        auto *h = new QHBoxLayout(clockRow);
        h->setContentsMargins(0, 27, 0, 27);
        h->setSpacing(16);
        auto *title = new QLabel(QStringLiteral("时钟制式"), clockRow);
        title->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
        title->setFixedWidth(170);
        h->addWidget(title);
        h->addStretch();
        auto *container = new QWidget(clockRow);
        container->setFixedSize(240, 44);
        container->setStyleSheet(init24h
            ? "QWidget{background:url(:/images/butt_setting_choose_right.png) no-repeat;}"
            : "QWidget{background:url(:/images/butt_setting_choose_left.png) no-repeat;}");
        auto *h12Btn = new QPushButton(QStringLiteral("12小时"), container);
        auto *h24Btn = new QPushButton(QStringLiteral("24小时"), container);
        h12Btn->setGeometry(0,   0, 120, 44);
        h24Btn->setGeometry(120, 0, 120, 44);
        const QString btnStyle =
            "QPushButton{border:none;background:transparent;color:#fff;font-size:24px;}"
            "QPushButton:hover{color:#00faff;}"
            "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;border:none;box-shadow:none;}";
        h12Btn->setStyleSheet(btnStyle);
        h24Btn->setStyleSheet(btnStyle);
        h12Btn->setCursor(Qt::PointingHandCursor);
        h24Btn->setCursor(Qt::PointingHandCursor);
        QObject::connect(h12Btn, &QPushButton::clicked, container, [container]() {
            container->setStyleSheet("QWidget{background:url(:/images/butt_setting_choose_left.png) no-repeat;}");
            qApp->setProperty("appClock24h", false);
            AppSignals::instance()->clockFormatChanged(false);
        });
        QObject::connect(h24Btn, &QPushButton::clicked, container, [container]() {
            container->setStyleSheet("QWidget{background:url(:/images/butt_setting_choose_right.png) no-repeat;}");
            qApp->setProperty("appClock24h", true);
            AppSignals::instance()->clockFormatChanged(true);
        });
        h->addWidget(container);
        layout->addWidget(clockRow);
        layout->addWidget(makeDivider());
    }

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
    // 初始值从全局 appVolumeLevel 同步（0..10 -> 0..100）
    const QVariant vp = qApp->property("appVolumeLevel");
    const int lv = vp.isValid() ? vp.toInt() : 10;
    const int initPerc = qBound(0, lv * 10, 100);
    slider->setValue(initPerc);
    slider->setFixedHeight(44);
    slider->setStyleSheet(
        "QSlider::groove:horizontal{height:8px;background:rgba(255,255,255,0.28);border-radius:4px;}"
        "QSlider::sub-page:horizontal{background:#00a5ff;border-radius:4px;}"
        "QSlider::handle:horizontal{background:url(:/images/pict_brightness_mark.png);width:24px;height:44px;margin:-18px 0;border:none;}"
    );
    auto *highIcon = new QLabel(vRight);
    highIcon->setFixedSize(36, 36);
    highIcon->setPixmap(QPixmap(":/images/pict_volume_loud.png").scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *tips = new QLabel(QString::number(initPerc), vRight);
    tips->setFixedSize(48, 38);
    tips->setAlignment(Qt::AlignCenter);
    tips->setStyleSheet("QLabel{background:url(:/images/pict_brightness_tips.png);font-size:24px;color:#fff;}");
    connect(slider, &QSlider::valueChanged, tips, [tips](int v) {
        tips->setText(QString::number(v));
    });
    // 用户拖动滑块时，设置系统音量（使用 amixer）
    connect(slider, &QSlider::valueChanged, this, [slider](int v) {
        // 将百分比 v 转发给 amixer（示例："50%"）
        AppSignals::runAmixer({"sset", "LINEOUT volume", QString::number(v) + "%"}, slider);
    });
    // 当外部（按键或其它窗口）改变音量时，更新滑块（避免产生 valueChanged 循环）
    connect(AppSignals::instance(), &AppSignals::volumeLevelChanged, this, [slider, tips](int level){
        const int perc = qBound(0, level * 10, 100);
        bool wasBlocked = slider->blockSignals(true);
        slider->setValue(perc);
        slider->blockSignals(wasBlocked);
        tips->setText(QString::number(perc));
    });
    vRightLayout->addWidget(lowIcon);
    vRightLayout->addWidget(slider, 1);
    vRightLayout->addWidget(highIcon);
    vRightLayout->addWidget(tips);
    volumeLayout->addWidget(vRight, 1);

    auto *volumeDivider = new QFrame(page);
    volumeDivider->setFixedHeight(2);
    volumeDivider->setStyleSheet("QFrame{background:rgba(255,255,255,0.1);border:none;}");
    layout->addWidget(volumeRow);
    layout->addWidget(volumeDivider);

    auto *touchRow = new QWidget(page);
    touchRow->setFixedHeight(98);
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
                    "QPushButton:hover{background:#0452ca;}"
                    "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;border:none;box-shadow:none;}")
                .arg(i == 0 ? "border-radius:22px 0 0 22px;" : (i == touchModes.size() - 1 ? "border-radius:0 22px 22px 0;" : "border-radius:0;"))
        );
        touchGroup->addButton(btn);
        tabLayout->addWidget(btn);
    }
    touchLayout->addWidget(tabWrap, 1);

    auto *touchDivider = new QFrame(page);
    touchDivider->setFixedHeight(2);
    touchDivider->setStyleSheet("QFrame{background:rgba(255,255,255,0.1);border:none;}");
    layout->addWidget(touchRow);
    layout->addWidget(touchDivider);

    auto *fieldRow = new QWidget(page);
    fieldRow->setFixedHeight(158);
    auto *fieldLayout = new QHBoxLayout(fieldRow);
    fieldLayout->setContentsMargins(0, 60, 0, 24);
    fieldLayout->setSpacing(16);
    auto *fieldTitle = new QLabel(QStringLiteral("声场模式"), fieldRow);
    fieldTitle->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    fieldTitle->setFixedWidth(170);
    fieldLayout->addWidget(fieldTitle);
    fieldLayout->addStretch();

    // CSS .setting_sound_field .setting_brightness_tab li:first-child { border-radius:100px; w:168; bg:#0452CA }
    // 使用专用图片（butt_setting_sound_field_up/down.png 168×44）
    auto *fieldBtn = new QPushButton(
        qApp->property("appSoundMode").toString().isEmpty()
            ? QStringLiteral("立体声")
            : qApp->property("appSoundMode").toString(),
        fieldRow);
    fieldBtn->setFixedSize(168, 44);
    fieldBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_setting_sound_field_up.png) no-repeat center;color:#fff;font-size:24px;}"
        "QPushButton:hover{background:url(:/images/butt_setting_sound_field_down.png) no-repeat center;}"
        "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;border:none;box-shadow:none;}"
    );
    connect(fieldBtn, &QPushButton::clicked, this, [this, fieldBtn]() {
        // 声场模式选择：全屏对话框，匹配 system_setting_sound_field.html
        // HTML: .radio_sound { width:696; margin:150px auto 0 }
        //       ul { height:330; flex-wrap:wrap; justify-content:space-between; align-content:space-around }
        //       li { width:200; height:78; border:1px solid #0068FF; font-size:35; line-height:76; text-align:center }
        QDialog dialog(this);
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setFixedSize(1280, 720);
        dialog.setStyleSheet("QDialog{background-image:url(:/images/inside_background.png);}");

        // 顶部栏（同主界面：topbar.png + HOME 按钮 + TopBarRightWidget）
        auto *topBar = new QWidget(&dialog);
        topBar->setGeometry(0, 0, 1280, 82);
        topBar->setStyleSheet("background-image:url(:/images/topbar.png);");
        auto *dlgHomeBtn = new QPushButton(topBar);
        dlgHomeBtn->setGeometry(12, 12, 48, 48);
        dlgHomeBtn->setStyleSheet(
            "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);background-repeat:no-repeat;}"
            "QPushButton:hover,QPushButton:pressed{background-image:url(:/images/pict_home_down.png);}");
        dlgHomeBtn->setCursor(Qt::PointingHandCursor);
        connect(dlgHomeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
        auto *dlgTitle = new QLabel(QStringLiteral("系统设置"), topBar);
        dlgTitle->setGeometry(0, 0, 1280, 72);
        dlgTitle->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
        dlgTitle->setAlignment(Qt::AlignCenter);
        dlgTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *dlgTopBarRight = new TopBarRightWidget(topBar);
        dlgTopBarRight->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                                    TopBarRightWidget::preferredWidth(), 48);

        // 返回按钮：.back { left:60; top:103; 60×60 }
        auto *backBtn = new QPushButton(&dialog);
        backBtn->setGeometry(60, 103, 60, 60);
        backBtn->setStyleSheet(
            "QPushButton{border:none;background:url(:/images/butt_back_up.png) no-repeat;}"
            "QPushButton:hover{background:url(:/images/butt_back_down.png) no-repeat;}");
        backBtn->setCursor(Qt::PointingHandCursor);
        connect(backBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

        // .radio_sound { width:696; margin:150px auto 0 } → x=(1280-696)/2=292, y=82+150=232
        // ul: height:330; items: width:200 height:78
        // 9 items × 3 per row × space-between in 696px → items at 0,248,496 per row
        const QStringList modes = {
            QStringLiteral("立体声"), QStringLiteral("环绕声"), QStringLiteral("低音增强"),
            QStringLiteral("高音增强"), QStringLiteral("平衡音"), QStringLiteral("音场定位"),
            QStringLiteral("降噪音效"), QStringLiteral("虚拟声场"), QStringLiteral("响度补偿")
        };
        const int currentIdx = modes.indexOf(fieldBtn->text());

        // row heights: 330/3=110px each; space-around → top/bottom gap = (330-3×78)/(3×2)=16px
        // 每行 y offset: 16, 16+78+32=126, 16+78+32+78+32=236
        const int colXs[3] = {0, 248, 496};
        const int rowYs[3] = {16, 126, 236};
                for (int i = 0; i < modes.size(); ++i) {
                        const int col = i % 3;
                        const int row = i / 3;
                        auto *btn = new QPushButton(modes[i], &dialog);
                        btn->setGeometry(292 + colXs[col], 232 + rowYs[row], 200, 78);
                        const bool isActive = (i == currentIdx);
                        btn->setStyleSheet(isActive
                                ? "QPushButton{border:2px solid #00FAFF;background:rgba(0,250,255,0.08);color:#00FAFF;font-size:35px;}"
                                    "QPushButton:hover{border:2px solid #00FAFF;color:#00FAFF;}"
                                    "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;box-shadow:none;}"
                                : "QPushButton{border:1px solid #0068FF;background:transparent;color:#fff;font-size:35px;}"
                                    "QPushButton:hover{border:2px solid #00FAFF;color:#00FAFF;}"
                                    "QPushButton:focus, QPushButton:checked:focus, QPushButton:focus-visible {outline:none;box-shadow:none;}"
                        );
                        btn->setCursor(Qt::PointingHandCursor);
                        connect(btn, &QPushButton::clicked, &dialog, [&dialog, fieldBtn, text = modes[i]]() {
                                fieldBtn->setText(text);
                                qApp->setProperty("appSoundMode", text);
                                T507SdkBridge::setSoundMode(text);
                                dialog.accept();
                        });
                }
        dialog.exec();
    });
    fieldLayout->addWidget(fieldBtn);

    layout->addWidget(fieldRow);

    auto *fieldDivider = new QFrame(page);
    fieldDivider->setFixedHeight(2);
    fieldDivider->setStyleSheet("QFrame{background:rgba(255,255,255,0.1);border:none;}");
    layout->addWidget(fieldDivider);

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
    tabs->tabBar()->setUsesScrollButtons(false);
    tabs->tabBar()->setExpanding(false);
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

    // HTML: 在移动设备上启动蓝牙，并搜索本设备name：<a>wodelanya</a><br>匹配密码：0000
    // CSS .bluetooth_intro a { color:#00FAFF }  设备名称不换行显示
    auto *deviceName = new QString(QStringLiteral("wodelanya"));
    auto *pairPwd = new QString(QStringLiteral("0000"));

    auto *intro = new QLabel(openTab);
    intro->setTextFormat(Qt::RichText);
    intro->setText(QStringLiteral(
        "在移动设备上启动蓝牙，并搜索本设备name：<a href='rename' style='color:#00FAFF;text-decoration:none;'>%1</a><br>匹配密码：%2"
    ).arg(*deviceName, *pairPwd));
    intro->setStyleSheet("QLabel{font-size:28px;line-height:42px;color:#eaf2ff;}");
    intro->setOpenExternalLinks(false);
    // 点击设备名称 → 重命名对话框
    connect(intro, &QLabel::linkActivated, this, [this, intro, deviceName, pairPwd](const QString &link) {
        if (link != QStringLiteral("rename")) return;
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
        auto *r = new QHBoxLayout(); r->addStretch(); r->addWidget(ok); r->addStretch();
        dLayout->addLayout(r);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() == QDialog::Accepted && !line->text().trimmed().isEmpty()) {
            *deviceName = line->text().trimmed();
            intro->setText(QStringLiteral(
                "在移动设备上启动蓝牙，并搜索本设备name：<a href='rename' style='color:#00FAFF;text-decoration:none;'>%1</a><br>匹配密码：%2"
            ).arg(*deviceName, *pairPwd));
        }
    });

    openLayout->addWidget(switchRow);
    openLayout->addWidget(intro);
    openLayout->addStretch();

    auto *pairTab = new QWidget();
    auto *pairLayout = new QVBoxLayout(pairTab);
    pairLayout->setContentsMargins(0, 0, 0, 0);
    pairLayout->setSpacing(0);

    // CSS .bluetooth_list>li { line-height:60; height:110 }  dt 左浮；dd { width:400 }
    // ── 第一行：当前连接的设备 ──
    auto *pairRow1 = new QWidget(pairTab);
    pairRow1->setFixedHeight(110);
    pairRow1->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.08);}");
    auto *pairRow1H = new QHBoxLayout(pairRow1);
    pairRow1H->setContentsMargins(0, 25, 0, 25);
    pairRow1H->setSpacing(0);
    auto *pairDt1 = new QLabel(QStringLiteral("当前连接的设备"), pairRow1);
    pairDt1->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    pairRow1H->addWidget(pairDt1);
    pairRow1H->addStretch();
    auto *currentDevice = new QLabel(QStringLiteral("手机：13723467654"), pairRow1);
    currentDevice->setFixedSize(400, 60);
    currentDevice->setAlignment(Qt::AlignCenter);
    currentDevice->setStyleSheet(
        "QLabel{height:60px;border:1px solid #0068FF;background:rgba(255,255,255,0.10);font-size:28px;color:#fff;}");
    pairRow1H->addWidget(currentDevice);

    // ── 第二行：选择连接手机（last-child: height:290; border:none）──
    auto *pairRow2 = new QWidget(pairTab);
    auto *pairRow2H = new QHBoxLayout(pairRow2);
    pairRow2H->setContentsMargins(0, 25, 0, 0);
    pairRow2H->setSpacing(0);
    auto *pairDt2 = new QLabel(QStringLiteral("请选择需要\n连接的手机"), pairRow2);
    pairDt2->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;line-height:48px;}");
    pairRow2H->addWidget(pairDt2);
    pairRow2H->addStretch();

    auto *pairDdWidget = new QWidget(pairRow2);
    pairDdWidget->setFixedWidth(400);
    auto *pairDdLayout = new QVBoxLayout(pairDdWidget);
    pairDdLayout->setContentsMargins(0, 0, 0, 0);
    pairDdLayout->setSpacing(0);

    auto *pairList = new QListWidget(pairDdWidget);
    pairList->addItems({QStringLiteral("手机：13723467654"), QStringLiteral("手机：13723467655"), QStringLiteral("手机：13723467656")});
    pairList->setCurrentRow(0);
    pairList->setFixedHeight(192);
    pairList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;font-size:28px;color:#fff;}"
        "QListWidget::item{height:60px;border:1px solid #0068FF;background:rgba(255,255,255,0.10);text-align:center;}"
        "QListWidget::item:selected{border:2px solid #00FAFF;color:#00FAFF;}"
        "QListWidget::item:hover{border:2px solid #00FAFF;color:#00FAFF;}"
        "QScrollBar:vertical{width:6px;background:rgba(0,104,255,0.10);border-radius:3px;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;background:none;}"
    );

    // CSS .bluetooth_list .page_btn { width:326; justify-content:space-between; margin:0 auto }
    // button.bluetooth_btn { w:112; h:54; border:2px solid #0068FF }
    auto *pairBtnWrap = new QWidget(pairDdWidget);
    pairBtnWrap->setFixedSize(326, 74);
    auto *pairBtnH = new QHBoxLayout(pairBtnWrap);
    pairBtnH->setContentsMargins(0, 20, 0, 0);
    pairBtnH->setSpacing(0);
    auto *connectBtn = makeBlueBtn(QStringLiteral("连接"));
    auto *disconnectBtn = makeBlueBtn(QStringLiteral("断开"));
    pairBtnH->addWidget(connectBtn);
    pairBtnH->addStretch();
    pairBtnH->addWidget(disconnectBtn);
    connect(connectBtn, &QPushButton::clicked, this, [pairList, currentDevice]() {
        if (pairList->currentItem())
            currentDevice->setText(pairList->currentItem()->text());
    });
    connect(disconnectBtn, &QPushButton::clicked, this, [currentDevice]() {
        currentDevice->setText(QStringLiteral("未连接"));
    });

    pairDdLayout->addWidget(pairList);
    pairDdLayout->addWidget(pairBtnWrap, 0, Qt::AlignHCenter);
    pairRow2H->addWidget(pairDdWidget);
    pairLayout->addWidget(pairRow1);
    pairLayout->addWidget(pairRow2);
    pairLayout->addStretch();

    // ── 删除设备 Tab ──────────────────────────────────────────────────────────
    // HTML: system_setting_bluetooth_device_del.html — 同样两行结构，确认按钮居中
    auto *removeTab = new QWidget();
    auto *removeLayout = new QVBoxLayout(removeTab);
    removeLayout->setContentsMargins(0, 0, 0, 0);
    removeLayout->setSpacing(0);

    // 第一行：当前连接的设备
    auto *delRow1 = new QWidget(removeTab);
    delRow1->setFixedHeight(110);
    delRow1->setStyleSheet("QWidget{border-bottom:2px solid rgba(255,255,255,0.08);}");
    auto *delRow1H = new QHBoxLayout(delRow1);
    delRow1H->setContentsMargins(0, 25, 0, 25);
    delRow1H->setSpacing(0);
    auto *delDt1 = new QLabel(QStringLiteral("当前连接的设备"), delRow1);
    delDt1->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;}");
    delRow1H->addWidget(delDt1);
    delRow1H->addStretch();
    auto *delCurrentDevice = new QLabel(QStringLiteral("手机：13723467654"), delRow1);
    delCurrentDevice->setFixedSize(400, 60);
    delCurrentDevice->setAlignment(Qt::AlignCenter);
    delCurrentDevice->setStyleSheet(
        "QLabel{height:60px;border:1px solid #0068FF;background:rgba(255,255,255,0.10);font-size:28px;color:#fff;}");
    delRow1H->addWidget(delCurrentDevice);

    // 第二行：选择要删除的设备
    auto *delRow2 = new QWidget(removeTab);
    auto *delRow2H = new QHBoxLayout(delRow2);
    delRow2H->setContentsMargins(0, 25, 0, 0);
    delRow2H->setSpacing(0);
    auto *delDt2 = new QLabel(QStringLiteral("请选择需要\n删除的手机"), delRow2);
    delDt2->setStyleSheet("QLabel{font-size:32px;color:#eaf2ff;line-height:48px;}");
    delRow2H->addWidget(delDt2);
    delRow2H->addStretch();

    auto *delDdWidget = new QWidget(delRow2);
    delDdWidget->setFixedWidth(400);
    auto *delDdLayout = new QVBoxLayout(delDdWidget);
    delDdLayout->setContentsMargins(0, 0, 0, 0);
    delDdLayout->setSpacing(0);

    auto *removeList = new QListWidget(delDdWidget);
    removeList->addItems({QStringLiteral("手机：13723467654"), QStringLiteral("手机：13723467655"), QStringLiteral("手机：13723467656")});
    removeList->setCurrentRow(0);
    removeList->setFixedHeight(216);
    removeList->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;font-size:28px;color:#fff;}"
        "QListWidget::item{height:60px;border:1px solid #0068FF;background:rgba(255,255,255,0.10);text-align:center;margin-bottom:12px;}"
        "QListWidget::item:selected{border:2px solid #00FAFF;color:#00FAFF;}"
        "QListWidget::item:hover{border:2px solid #00FAFF;color:#00FAFF;}"
        "QScrollBar:vertical{width:6px;background:rgba(0,104,255,0.10);border-radius:3px;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;background:none;}"
    );

    // HTML: justify-content:center → 只有一个确认按钮居中
    auto *delBtnWrap = new QWidget(delDdWidget);
    delBtnWrap->setFixedSize(326, 74);
    auto *delBtnH = new QHBoxLayout(delBtnWrap);
    delBtnH->setContentsMargins(0, 20, 0, 0);
    auto *removeBtn = makeBlueBtn(QStringLiteral("确认"));
    delBtnH->addStretch();
    delBtnH->addWidget(removeBtn);
    delBtnH->addStretch();
    connect(removeBtn, &QPushButton::clicked, this, [removeList, delCurrentDevice]() {
        if (removeList->currentRow() >= 0) {
            delete removeList->takeItem(removeList->currentRow());
            if (removeList->count() == 0)
                delCurrentDevice->setText(QStringLiteral("未连接"));
        }
    });

    delDdLayout->addWidget(removeList);
    delDdLayout->addWidget(delBtnWrap, 0, Qt::AlignHCenter);
    delRow2H->addWidget(delDdWidget);
    removeLayout->addWidget(delRow1);
    removeLayout->addWidget(delRow2);
    removeLayout->addStretch();

    // ── 密码设置 Tab ──────────────────────────────────────────────────────────
    // HTML: .bluetooth_passward { width:840px; padding:0 12px } → 816px 可用宽度
    //   input: height:72px; font-size:48px; padding-left:24px; border:1px solid #0068FF
    //   .radio_search_keybord ul (610px, 3×4, 198×94) + phone_dial_btn (198px)
    //   justify-content:space-between → 610 + gap + 198 = 816px
    auto *passwordTab = new QWidget();
    // 外层：水平居中一个 840px 容器
    auto *pwdOuterLayout = new QHBoxLayout(passwordTab);
    pwdOuterLayout->setContentsMargins(0, 0, 0, 0);
    pwdOuterLayout->setSpacing(0);
    auto *pwdContainer = new QWidget();
    pwdContainer->setFixedWidth(840);
    auto *passwordLayout = new QVBoxLayout(pwdContainer);
    // 12px padding 两侧 → 816px 内容宽
    passwordLayout->setContentsMargins(12, 0, 12, 0);
    passwordLayout->setSpacing(0);
    pwdOuterLayout->addStretch();
    pwdOuterLayout->addWidget(pwdContainer);
    pwdOuterLayout->addStretch();

    // ─ 输入框行：QLineEdit(816px, h:72) + 清除按钮绝对叠放在右端 ─
    // CSS .bluetooth_passward div input { padding-left:24px; font-size:48px; height:72px }
    //     .radio_search_con>div>span { right:24px; top:12px; 48×48 }
    auto *pwdInputWrap = new QWidget(pwdContainer);
    pwdInputWrap->setFixedHeight(80);
    auto *pwdEdit = new QLineEdit(QStringLiteral("1234"), pwdInputWrap);
    pwdEdit->setReadOnly(true);
    pwdEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pwdEdit->setGeometry(0, 4, 816, 72);
    pwdEdit->setStyleSheet(
        "QLineEdit{height:72px;border:1px solid #0068FF;color:#fff;font-size:48px;"
        "padding-left:24px;background:transparent;}");
    // 清除按钮叠放：right:24px top:12px → x = 816-24-48=744, y = 4+12=16
    auto *pwdClear = new QPushButton(pwdInputWrap);
    pwdClear->setGeometry(744, 16, 48, 48);
    pwdClear->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_radio_search_del_all_up.png) no-repeat center;}"
        "QPushButton:hover{background:url(:/images/butt_radio_search_del_all_down.png) no-repeat center;}");
    connect(pwdClear, &QPushButton::clicked, this, [pwdEdit](){ pwdEdit->clear(); });

    // ─ 键盘区域 (.radio_search_keybord padding-top:8px) ─
    // 816px = 610(ul) + space-between + 198(phone_dial_btn)
    auto *kbdArea = new QWidget(pwdContainer);
    kbdArea->setFixedSize(816, 408);
    auto *kbdH = new QHBoxLayout(kbdArea);
    kbdH->setContentsMargins(0, 8, 0, 0);
    kbdH->setSpacing(0);

    // ─ 左侧 3×4 主键 (610×400, space-between) ─
    // key 198×94; x=[0,206,412]; y=[0,102,204,306]
    // 数字 span: font-size:48px bold, line-height:48px, margin-top:11px
    // 字母 span: font-size:22px normal, line-height:22px, margin-top:2px
    struct KeyDef { QString num; QString letters; };
    const QList<KeyDef> keyDefs = {
        {"1",""},{"2","ABC"},{"3","DEF"},
        {"4","GHI"},{"5","JKL"},{"6","MNO"},
        {"7","PQRS"},{"8","TUV"},{"9","WXYZ"},
        {"*",""},{"0","+"},{"#",""}
    };
    auto *keysWidget = new QWidget(kbdArea);
    keysWidget->setFixedSize(610, 400);
    const int kxs[3] = {0, 206, 412};
    const int kys[4] = {0, 102, 204, 306};
    for (int i = 0; i < keyDefs.size(); ++i) {
        const int col = i % 3;
        const int krow = i / 3;
        const bool isStar    = (keyDefs[i].num == QStringLiteral("*"));
        const bool hasLetters = !keyDefs[i].letters.isEmpty();
        auto *btn = new QPushButton(keysWidget);
        btn->setGeometry(kxs[col], kys[krow], 198, 94);
        // QPushButton 只提供边框和点击事件，文字由 QLabel 叠加渲染
        btn->setStyleSheet(
            "QPushButton{border:1px solid #0068FF;background:rgba(255,255,255,0.03);}"
            "QPushButton:hover{border:1px solid #00FAFF;}");
        if (isStar) {
            // CSS inline: line-height:120px; font-size:64px
            auto *lbl = new QLabel(QStringLiteral("*"), btn);
            lbl->setGeometry(0, 0, 198, 94);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("font-size:64px;color:#fff;background:transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        } else if (hasLetters) {
            // 上：数字 48px bold（margin-top:11, line-height:48）→ y=8, h=52
            auto *numLbl = new QLabel(keyDefs[i].num, btn);
            numLbl->setGeometry(0, 8, 198, 52);
            numLbl->setAlignment(Qt::AlignCenter);
            numLbl->setStyleSheet("font-size:48px;font-weight:bold;color:#fff;background:transparent;");
            numLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            // 下：字母 22px normal（margin-top:2）→ y=60, h=26
            auto *letLbl = new QLabel(keyDefs[i].letters, btn);
            letLbl->setGeometry(0, 60, 198, 26);
            letLbl->setAlignment(Qt::AlignCenter);
            letLbl->setStyleSheet("font-size:22px;color:#fff;background:transparent;");
            letLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        } else {
            // 1, # ：单行数字居中 48px bold
            auto *lbl = new QLabel(keyDefs[i].num, btn);
            lbl->setGeometry(0, 0, 198, 94);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("font-size:48px;font-weight:bold;color:#fff;background:transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        connect(btn, &QPushButton::clicked, this, [pwdEdit, num=keyDefs[i].num](){
            pwdEdit->setText((pwdEdit->text() + num).right(8));
        });
    }
    kbdH->addWidget(keysWidget);
    kbdH->addStretch();

    // ─ 右侧 phone_dial_btn (198×400) ─
    // li:first-child { h:196; margin-bottom:8px; bg:butt_calling_del_up.png }
    // li:last-child  { h:196; bg:#0068FF; font-size:48px; line-height:198px(居中) }
    auto *dialBtn = new QWidget(kbdArea);
    dialBtn->setFixedSize(198, 400);
    auto *backspaceBtn = new QPushButton(dialBtn);
    backspaceBtn->setGeometry(0, 0, 198, 196);
    backspaceBtn->setStyleSheet(
        "QPushButton{border:none;background:url(:/images/butt_calling_del_up.png) no-repeat center;}"
        "QPushButton:hover{background:url(:/images/butt_calling_del_down.png) no-repeat center;}");
    connect(backspaceBtn, &QPushButton::clicked, this, [pwdEdit](){
        pwdEdit->setText(pwdEdit->text().left(qMax(0, pwdEdit->text().size()-1)));
    });
    auto *confirmBtn = new QPushButton(QStringLiteral("确认"), dialBtn);
    confirmBtn->setGeometry(0, 204, 198, 196);
    confirmBtn->setStyleSheet(
        "QPushButton{border:none;background:#0068FF;color:#fff;font-size:48px;font-weight:bold;}"
        "QPushButton:hover{background:#00FAFF;}");
    connect(confirmBtn, &QPushButton::clicked, this, [pairPwd, pwdEdit, intro, deviceName](){
        if (!pwdEdit->text().isEmpty()) {
            *pairPwd = pwdEdit->text();
            intro->setText(QStringLiteral(
                "在移动设备上启动蓝牙，并搜索本设备name：<a href='rename' style='color:#00FAFF;text-decoration:none;'>%1</a><br>匹配密码：%2"
            ).arg(*deviceName, *pairPwd));
        }
    });
    kbdH->addWidget(dialBtn);

    passwordLayout->addWidget(pwdInputWrap);
    passwordLayout->addWidget(kbdArea);
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
    // 软件版本：从 U-Boot 环境变量读取（fw_printenv -n swu_version）
    auto readSoftVersion = []() -> QString {
        QProcess p;
        p.start(QStringLiteral("fw_printenv"), {QStringLiteral("-n"), QStringLiteral("swu_version")});
        if (p.waitForFinished(800)) {
            const QString v = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
            if (!v.isEmpty()) return v;
        }
        return QStringLiteral("-");
    };

    // 硬件型号：从设备树读取
    auto readHardwareModel = []() -> QString {
        QFile f(QStringLiteral("/proc/device-tree/model"));
        if (f.open(QIODevice::ReadOnly)) {
            const QString m = QString::fromLocal8Bit(f.readAll()).trimmed().replace(QChar('\0'), QString());
            f.close();
            if (!m.isEmpty()) return m;
        }
        return QStringLiteral("-");
    };

    // 系统更新日期：优先读 OTA 写入的 swu_date，无则用编译时日期
    auto readUpdateDate = []() -> QString {
        QProcess p;
        p.start(QStringLiteral("fw_printenv"), {QStringLiteral("-n"), QStringLiteral("swu_date")});
        if (p.waitForFinished(800)) {
            const QString d = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
            if (!d.isEmpty()) return d;
        }
        return QStringLiteral(APP_BUILD_DATE);
    };

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
        {QStringLiteral("软件版本："), readSoftVersion()},
        {QStringLiteral("硬件型号："), readHardwareModel()},
        {QStringLiteral("系统更新日期："), readUpdateDate()}
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

        connect(ok, &QPushButton::clicked, &dialog, [this, &dialog]() {
            performFactoryReset();
            dialog.accept();
            emit requestReturnToMain();
            this->close();
        });
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

void SystemSettingWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, this);
        break;
    case Qt::Key_VolumeDown:
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, this);
        break;
    case Qt::Key_HomePage:
    case Qt::Key_Back:
    case Qt::Key_Escape:
        emit requestReturnToMain();
        close();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
