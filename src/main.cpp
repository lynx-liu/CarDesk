#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QTextCodec>
#include <QTimer>
#include <QDialog>
#include <fcntl.h>
#include <unistd.h>
#include <QSocketNotifier>
#include "backlight.h"
#include "t507sdkbridge.h"
#include <linux/input.h>
#include "mainwindow.h"
#include "devicedetect.h"
#include "appsignals.h"

// ── 背光控制（POWER 键关/亮屏，SLEEP 键关机，具体 dispdbg 操作在 backlight.cpp）─
class ScreenBlanker : public QObject {
public:
    static ScreenBlanker *instance() {
        static ScreenBlanker s;
        return &s;
    }
    bool isBlanked() const { return m_blanked; }

    void blank() {
        if (m_blanked) return;
        // 保存当前亮度（来自 Backlight 缓存或 sysfs）
        m_savedBrightness = Backlight::get();
        qDebug() << "[ScreenBlanker] blank: saved brightness=" << m_savedBrightness;
        m_blanked = true;
        Backlight::set(0);
    }
    void unblank() {
        if (!m_blanked) return;
        m_blanked = false;
        Backlight::set(m_savedBrightness);
        qDebug() << "[ScreenBlanker] unblank: restore brightness=" << m_savedBrightness;
    }
    void toggle() {
        if (m_blanked) unblank(); else blank();
    }

private:
    bool m_blanked = false;
    int  m_savedBrightness = 128;
};

// ── 音量浮动指示条 ────────────────────────────────────────────────────────────
// 按下音量键时显示在屏幕左侧，2 秒无操作后自动隐藏
class VolumeOverlay : public QWidget {
public:
    explicit VolumeOverlay()
        : QWidget(nullptr,
                  Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
        , m_percent(50)
        , m_hideTimer(new QTimer(this))
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);  // 不抢焦点
        setFixedSize(24, 330);  // 宽度缩半（48→24），高度×1.5（220→330）
        m_hideTimer->setSingleShot(true);
        connect(m_hideTimer, &QTimer::timeout, this, &VolumeOverlay::hide);
    }

    // 传入 0-100 的百分比，显示并重置自动隐藏计时器
    void showVolume(int percent) {
        m_percent = qBound(0, percent, 100);
        // 定位到屏幕左侧垂直居中
        QScreen *sc = QGuiApplication::primaryScreen();
        if (sc) {
            const QRect sg = sc->geometry();
            move(sg.x() + 20, sg.y() + (sg.height() - height()) / 2);
        }
        update();
        show();
        raise();
        m_hideTimer->start(2000);  // 松开 2 秒后自动隐藏
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRect r = rect();
        const int padding  = 6;
        const int textH    = 22;
        const int barX     = padding;
        const int barW     = r.width() - padding * 2;
        const int barTop   = padding;
        const int barBot   = r.height() - padding - textH - 4;
        const int barTotalH = barBot - barTop;

        // 背景圆角矩形
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 180));
        p.drawRoundedRect(r, 10, 10);

        // 轨道（空槽）
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(barX, barTop, barW, barTotalH, 4, 4);

        // 已填充部分（从底部向上）
        int fillH = barTotalH * m_percent / 100;
        if (fillH > 0) {
            p.setBrush(QColor(0, 104, 255));  // 主题色 #0068FF
            p.drawRoundedRect(barX, barTop + barTotalH - fillH, barW, fillH, 4, 4);
        }

        // 百分比文字
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, barBot + 4, r.width(), textH),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(m_percent));
    }

private:
    int     m_percent;
    QTimer *m_hideTimer;
};

// 异步读取当前音量百分比（在 amixer set 之后 100ms 延迟再读，等待 set 完成）
static void scheduleVolumeRead(VolumeOverlay *overlay) {
    QTimer::singleShot(120, overlay, [overlay]() {
        auto *proc = new QProcess(overlay);
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         overlay, [proc, overlay](int) {
            const QString out = QString::fromLocal8Bit(proc->readAllStandardOutput());
            proc->deleteLater();
            // 解析 "[87%]" 格式
            const int lb = out.lastIndexOf('[');
            const int pct = out.indexOf('%', lb);
            if (lb >= 0 && pct > lb) {
                bool ok = false;
                int v = out.mid(lb + 1, pct - lb - 1).toInt(&ok);
                if (ok) {
                    overlay->showVolume(v);
                    // 同步更新全局音量等级（各界面顶部栏可读取）
                    const int lv = qBound(0, (v + 5) / 10, 10);
                    QCoreApplication::instance()->setProperty("appVolumeLevel", lv);
                    // 通知所有已注册的顶部栏实时刷新音量显示
                    AppSignals::instance()->volumeLevelChanged(lv);
                }
            }
        });
        proc->start("amixer", {"sget", "LINEOUT volume"});
    });
}

// 挂在 QApplication 上，能拦截所有窗口的 KeyPress，不依赖窗口焦点
class GlobalKeyFilter : public QObject {
public:
    explicit GlobalKeyFilter(VolumeOverlay *overlay, QObject *parent = nullptr)
        : QObject(parent), m_overlay(overlay) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // 任意按键或触摸 → 完成亮屏
        const QEvent::Type t = event->type();
        if (t == QEvent::MouseButtonPress || t == QEvent::TouchBegin) {
            if (ScreenBlanker::instance()->isBlanked()) {
                ScreenBlanker::instance()->unblank();
                return true;  // 吸收事件，防止触发底层操作
            }
        }
        if (t == QEvent::KeyPress) {
            // 任意按键亮屏——但电源/睡眠键由 InputNotifier raw 路径处理（toggle/poweroff）。
            // 这里只消耗事件、不亮屏，避免两条路径叠加导致闪屏后又关屏。
            // 双重判断：scanCode（raw evdev code）和 Qt key value（evdevkeyboard 可能
            // 将 KEY_POWER→Key_PowerOff，其 nativeScanCode 不保证等于 116）。
            if (ScreenBlanker::instance()->isBlanked()) {
                QKeyEvent *ke = static_cast<QKeyEvent *>(event);
                const unsigned int sc = ke->nativeScanCode();
                const int k = ke->key();
                const bool isPowerKey = (sc == 116u || sc == 142u
                    || k == Qt::Key_PowerOff || k == Qt::Key_Sleep
                    || k == Qt::Key_WakeUp   || k == Qt::Key_PowerDown);
                if (isPowerKey) {
                    return true;  // 消耗事件，亮屏由 InputNotifier::toggle() 完成
                }
                ScreenBlanker::instance()->unblank();
                return true;
            }
            QKeyEvent *ke = static_cast<QKeyEvent *>(event);
            qDebug() << "[GlobalKey] type=KeyPress"
                     << "key=" << ke->key()
                     << "nativeScanCode=" << ke->nativeScanCode()
                     << "nativeVirtualKey=" << ke->nativeVirtualKey()
                     << "watched=" << watched->metaObject()->className();

            const int key = ke->key();

            // ── QDialog 内按键拦截 ───────────────────────────────────────────
            // 搜索/收藏等子对话框本身不处理 Back/HomePage，在此统一处理：
            //   Back  → 关闭对话框（相当于点返回按钮）
            //   HOME  → 关闭对话框，再向父窗口转发 HomePage（父窗口的
            //            keyPressEvent 会执行 emit requestReturnToMain）
            if (key == Qt::Key_Back || key == Qt::Key_HomePage) {
                if (QDialog *dlg = qobject_cast<QDialog *>(QApplication::activeWindow())) {
                    qDebug() << "[GlobalKey] QDialog active, key=" << key << "=> reject()";
                    dlg->reject();
                    if (key == Qt::Key_HomePage) {
                        QWidget *parentWin = dlg->parentWidget()
                            ? dlg->parentWidget()->window() : nullptr;
                        if (parentWin) {
                            QApplication::postEvent(parentWin,
                                new QKeyEvent(QEvent::KeyPress, Qt::Key_HomePage, Qt::NoModifier));
                        }
                    }
                    return true;
                }
            }
            // ────────────────────────────────────────────────────────────────

            switch (key) {
            case Qt::Key_VolumeUp:
                qDebug() << "[GlobalKey] => VolumeUp";
                AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, nullptr);
                return true;
            case Qt::Key_VolumeDown:
                qDebug() << "[GlobalKey] => VolumeDown";
                AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, nullptr);
                return true;
            case Qt::Key_HomePage:
                qDebug() << "[GlobalKey] => HomePage";
                break;  // 交给各窗口 keyPressEvent 处理
            case Qt::Key_Back:
                qDebug() << "[GlobalKey] => Back";
                break;  // 交给各窗口 keyPressEvent 处理
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    VolumeOverlay *m_overlay = nullptr;
};

static void preloadSystemFonts() {
    const QString bundledFontDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../fonts"));
    if (QFileInfo(bundledFontDir).isDir()) {
        QDirIterator bundledIt(bundledFontDir,
                               QStringList() << QStringLiteral("*.ttf") << QStringLiteral("*.ttc") << QStringLiteral("*.otf"),
                               QDir::Files,
                               QDirIterator::NoIteratorFlags);
        while (bundledIt.hasNext()) {
            const QString filePath = bundledIt.next();
            const int fontId = QFontDatabase::addApplicationFont(filePath);
            if (fontId >= 0) {
                qDebug() << "Loaded bundled font file:" << filePath
                         << "families:" << QFontDatabase::applicationFontFamilies(fontId);
            }
        }
    }

    const QStringList fontFiles = {
        QStringLiteral("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"),
        QStringLiteral("/usr/share/fonts/truetype/arphic/ukai.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf"),
        QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        QStringLiteral("/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf"),
        QStringLiteral("/usr/share/fonts/DroidSansFallback.ttf")
    };

    for (const QString &fontFile : fontFiles) {
        if (QFileInfo::exists(fontFile)) {
            const int fontId = QFontDatabase::addApplicationFont(fontFile);
            if (fontId >= 0) {
                qDebug() << "Loaded font file:" << fontFile
                         << "families:" << QFontDatabase::applicationFontFamilies(fontId);
            }
        }
    }

    if (!QFontDatabase().families().isEmpty()) {
        return;
    }

    const QStringList fontDirs = {
        QStringLiteral("/usr/share/fonts"),
        QStringLiteral("/usr/local/share/fonts")
    };

    for (const QString &fontDir : fontDirs) {
        if (!QFileInfo(fontDir).isDir()) {
            continue;
        }
        QDirIterator it(fontDir,
                        QStringList() << QStringLiteral("*.ttf") << QStringLiteral("*.ttc") << QStringLiteral("*.otf"),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        int loadedCount = 0;
        while (it.hasNext() && loadedCount < 8) {
            const QString filePath = it.next();
            const int fontId = QFontDatabase::addApplicationFont(filePath);
            if (fontId >= 0) {
                ++loadedCount;
                qDebug() << "Loaded fallback font file:" << filePath;
            }
        }
    }
}

static QString pickAvailableFontFamily() {
    const QFontDatabase fontDb;
    const QStringList families = fontDb.families();
    const QStringList preferredFamilies = {
        QStringLiteral("思源黑体"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("Droid Sans Fallback"),
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("Sans Serif")
    };

    for (const QString &family : preferredFamilies) {
        if (families.contains(family)) {
            return family;
        }
    }

    return families.isEmpty() ? QString() : families.first();
}

static void configureApplicationFont(QApplication &app) {
    preloadSystemFonts();
    const QFontDatabase fontDb;
    qDebug() << "Available font family count:" << fontDb.families().size();

    const QString family = pickAvailableFontFamily();
    if (family.isEmpty()) {
        qWarning() << "No usable font family found. UI text may not render.";
        return;
    }

    QFont font(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    qDebug() << "Using application font:" << family;
}

int main(int argc, char *argv[]) {
    // eglfs 平台下 Qt 不自动扫描 /dev/input，需在 QApplication 构造前设置。
    // 所有硬件按键（HOME/BACK/VOL+/-/POWER）均来自 event3（goodix-ts 虚拟按键区）。
    // 外部未设置时才覆盖，允许 run.sh 或环境变量优先级更高。
    if (qgetenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS").isEmpty()) {
        qputenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS", "/dev/input/event3");
    }

    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // 设备 buildroot 默认 LANG=C，强制 UTF-8 避免中文文件名乱码
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    app.setOverrideCursor(Qt::BlankCursor);  // 触控设备隐藏鼠标指针
    app.setProperty("appClock24h", false);  // 默认 12 小时制（与原始行为一致）
    app.setProperty("appSoundMode", QStringLiteral("立体声"));  // 默认声场模式
    T507SdkBridge::setSoundMode(QStringLiteral("立体声"));  // 应用默认声场到 TM2313
    configureApplicationFont(app);

    // 全局硬件键监听（音量键 + 诊断日志）
    auto *volumeOverlay = new VolumeOverlay();
    app.installEventFilter(new GlobalKeyFilter(volumeOverlay, &app));

    // Qt 5.12 evdevkeyboard 默认 keymap 里没有 KEY_HOMEPAGE(172) 和 KEY_BACK(158)，
    // 直接用第二个 fd + QSocketNotifier 读 event3 来补全这两个键。
    // 与 evdevkeyboard 共用同一设备不冲突（内核允许多个 reader）。
    {
        const char *kbDev = "/dev/input/event3";
        int kbFd = ::open(kbDev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (kbFd >= 0) {
            qDebug() << "[InputNotifier] opened" << kbDev;
            auto *notifier = new QSocketNotifier(kbFd, QSocketNotifier::Read, &app);
            QObject::connect(notifier, &QSocketNotifier::activated, &app, [kbFd]() {
                struct input_event ev;
                while (::read(kbFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                    if (ev.type != EV_KEY || ev.value != 1) continue; // 只处理 key-down
                    int qtKey = 0;
                    switch (ev.code) {
                    case KEY_HOMEPAGE: qtKey = Qt::Key_HomePage; break;
                    case KEY_BACK:     qtKey = Qt::Key_Back;     break;
                    case KEY_SLEEP:
                        qDebug() << "[InputNotifier] ev.code=142 KEY_SLEEP => shutdown";
                        QProcess::startDetached(QStringLiteral("poweroff"), {});
                        break;
                    case KEY_POWER:
                        qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => toggle blank";
                        ScreenBlanker::instance()->toggle();
                        break;
                    default: break;
                    }
                    if (qtKey == 0) continue;
                    qDebug() << "[InputNotifier] ev.code=" << ev.code << "=> qtKey=" << qtKey;
                    // 找当前可见的顶层窗口（eglfs 上 activeWindow() 可能为 null）
                    QWidget *w = QApplication::activeWindow();
                    if (!w) {
                        for (QWidget *tw : QApplication::topLevelWidgets()) {
                            if (tw->isVisible() && tw->isWindow()) { w = tw; break; }
                        }
                    }
                    if (w) {
                        QApplication::postEvent(w,
                            new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier));
                        QApplication::postEvent(w,
                            new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier));
                    }
                }
            });
        } else {
            qWarning() << "[InputNotifier] failed to open" << kbDev;
        }
    }
    // 检测设备信息
    const DeviceDetect &device = DeviceDetect::instance();
    
    qDebug() << "========== CarDesk Application Started ==========";
    qDebug() << "Device Type:" << device.getDeviceTypeString();
    qDebug() << "Platform:" << device.getPlatform();
    qDebug() << "Architecture:" << device.getArchitecture();
    qDebug() << "Screen Resolution:" << device.getScreenWidth() 
             << "x" << device.getScreenHeight();
    qDebug() << "Touch Device:" << (device.isTouchDevice() ? "Yes" : "No");
    qDebug() << "==============================================";
    
// 启动时同步读取实际音量 (0-10 级)，存入应用属性供各界面颈部栏展示
    {
        QProcess volProc;
        volProc.start("amixer", {"sget", "LINEOUT volume"});
        int volLv = 10;
        if (volProc.waitForFinished(400)) {
            const QString vo = QString::fromLocal8Bit(volProc.readAllStandardOutput());
            const int lb = vo.lastIndexOf('['), pc = vo.indexOf('%', lb);
            if (lb >= 0 && pc > lb) {
                bool ok;
                const int v = vo.mid(lb + 1, pc - lb - 1).toInt(&ok);
                if (ok) volLv = qBound(0, (v + 5) / 10, 10);
            }
        }
        app.setProperty("appVolumeLevel", volLv);
    }

    MainWindow window;
    window.show();
    
    return app.exec();
}
