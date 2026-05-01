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
#include <QSettings>
#include <QTime>
#include <QSocketNotifier>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include "backlight.h"
#include "t507sdkbridge.h"
#include <linux/input.h>
#include "mainwindow.h"
#include "mediamanager.h"
#include "phonewindow.h"
#include "drivingimagewindow.h"
#include "musicplayerwindow.h"
#include "radiowindow.h"
#include "videolistwindow.h"
#include "videoplaywindow.h"
#include "devicedetect.h"
#include "appsignals.h"

// ── 背光控制（POWER 键关/亮屏，SLEEP 键关机，具体 dispdbg 操作在 backlight.cpp）─
static MainWindow *findMainWindow();
static DrivingImageWindow *findDrivingImageWindow();
static void hideClockOverlayIfVisible(bool resume = true);
class ScreenClockOverlay;

class ScreenBlanker : public QObject {
public:
    static ScreenBlanker *instance() {
        static ScreenBlanker s;
        return &s;
    }
    bool isBlanked() const { return m_blanked; }

    void blank() {
        if (m_blanked) return;
        qDebug() << "[ScreenBlanker] blank: hiding clock overlay and pausing playback";
        hideClockOverlayIfVisible(false);
        if (MainWindow *main = findMainWindow()) {
            if (main->mediaManager()) {
                QTimer::singleShot(0, this, [main]() {
                    qDebug() << "[ScreenBlanker] blank: executing deferred pausePlaybackForInterruption";
                    main->mediaManager()->pausePlaybackForInterruption();
                });
            }
        }
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
        hideClockOverlayIfVisible(false);
        if (MainWindow *main = findMainWindow()) {
            if (main->mediaManager()) {
                QTimer::singleShot(0, this, [main]() {
                    qDebug() << "[ScreenBlanker] unblank: executing deferred resumePlaybackAfterInterruption";
                    main->mediaManager()->resumePlaybackAfterInterruption();
                });
            }
        }
    }
    void toggle() {
        if (m_blanked) unblank(); else blank();
    }

private:
    bool m_blanked = false;
    int  m_savedBrightness = 128;
};

class ScreenClockOverlay : public QWidget {
public:
    static ScreenClockOverlay *instance() {
        static ScreenClockOverlay s;
        return &s;
    }

    void toggle() {
        if (isVisible()) {
            hideClock();
        } else {
            showClock();
        }
    }

    void showClock() {
        if (ScreenBlanker::instance()->isBlanked()) {
            ScreenBlanker::instance()->unblank();
        }
        if (DrivingImageWindow *drive = findDrivingImageWindow()) {
            if (drive->isVisible()) {
                qDebug() << "[ScreenClockOverlay] closing driving image before showing clock";
                drive->close();
            }
        }
        pauseBackgroundPlayback();
        updateMode();
        if (!isVisible()) {
            if (QScreen *sc = QGuiApplication::primaryScreen()) {
                setGeometry(sc->geometry());
            }
            show();
            raise();
            activateWindow();
            setFocus(Qt::ActiveWindowFocusReason);
            m_updateTimer.start();
            update();
        }
    }

    void pauseBackgroundPlayback() {
        if (MainWindow *main = findMainWindow()) {
            if (main->mediaManager()) {
                main->mediaManager()->pausePlaybackForOcclusion();
            }
        }
    }

    void hideClock(bool resume = true) {
        if (isVisible()) {
            hide();
            m_updateTimer.stop();
            if (resume) {
                if (MainWindow *main = findMainWindow()) {
                    if (main->mediaManager()) {
                        qDebug() << "[ScreenClockOverlay] hideClock: scheduling resumePlaybackAfterInterruption";
                        QTimer::singleShot(0, this, [main]() {
                            if (main->mediaManager()) {
                                qDebug() << "[ScreenClockOverlay] hideClock: executing resumePlaybackAfterInterruption";
                                main->mediaManager()->resumePlaybackAfterInterruption();
                            }
                        });
                    }
                }
            }
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::black);

        const QTime current = QTime::currentTime();
        if (m_digitalMode) {
            const QString text = current.toString(AppSignals::timeFormat());
            QFont font = p.font();
            font.setPixelSize(qMax(160, qMin(width(), height()) / 3));
            font.setWeight(QFont::ExtraBold);
            p.setFont(font);
            p.setPen(QColor(0x60, 0x7E, 0x9D));
            p.drawText(rect(), Qt::AlignCenter, text);
            return;
        }

        const int squareSize = qMin(width(), height());
        const QRect squareRect((width() - squareSize) / 2,
                               (height() - squareSize) / 2,
                               squareSize,
                               squareSize);
        const QRect dialRect = squareRect.adjusted(40, 40, -40, -40);
        const QPixmap dialPixmap(":/images/pict_clock_bg.png");
        if (!dialPixmap.isNull()) {
            p.drawPixmap(dialRect, dialPixmap.scaled(dialRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }

        const QPoint center = dialRect.center();
        const int radius = qMin(dialRect.width(), dialRect.height()) / 2;
        p.translate(center);

        const qreal hourAngle = (current.hour() % 12 + current.minute() / 60.0) * 30.0;
        const qreal minuteAngle = (current.minute() + current.second() / 60.0) * 6.0;
        const qreal secondAngle = current.second() * 6.0;

        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.save();
        p.rotate(hourAngle);
        p.drawRoundedRect(-8, -radius * 0.28, 16, radius * 0.28, 8, 8);
        p.restore();

        const qreal minuteWidth = 6.0;
        const qreal minuteLength = radius * 0.40;
        p.save();
        p.rotate(minuteAngle);
        {
            QPainterPath path;
            path.moveTo(-minuteWidth, 0);
            path.lineTo(-minuteWidth, -minuteLength * 0.32);
            path.lineTo(-minuteWidth * 0.35, -minuteLength * 0.76);
            path.lineTo(0, -minuteLength);
            path.lineTo(minuteWidth * 0.35, -minuteLength * 0.76);
            path.lineTo(minuteWidth, -minuteLength * 0.32);
            path.lineTo(minuteWidth, 0);
            path.closeSubpath();
            p.drawPath(path);
        }
        p.restore();

        p.setPen(QPen(QColor(0xFF, 0x00, 0x00), 4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.save();
        p.rotate(secondAngle);
        p.drawLine(QPointF(0, radius * 0.18), QPointF(0, -radius * 0.64));
        p.restore();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xFF, 0x00, 0x00));
        p.drawEllipse(QPoint(0, 0), 5, 5);
    }

    void keyPressEvent(QKeyEvent *event) override {
        Q_UNUSED(event);
        hideClock();
    }

    void mousePressEvent(QMouseEvent *event) override {
        Q_UNUSED(event);
        hideClock();
    }

    bool event(QEvent *event) override {
        if (event->type() == QEvent::TouchBegin) {
            hideClock();
            return true;
        }
        return QWidget::event(event);
    }

private:
    ScreenClockOverlay()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
        , m_updateTimer(this)
        , m_digitalMode(true)
    {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_AcceptTouchEvents);
        m_updateTimer.setInterval(1000);
        connect(&m_updateTimer, &QTimer::timeout, this, [this]() { update(); });
    }

    void updateMode() {
        const QString mode = qApp->property("appScreenClockMode").toString();
        m_digitalMode = (mode != QLatin1String("analog"));
    }

    QTimer m_updateTimer;
    bool m_digitalMode;
};

static void hideClockOverlayIfVisible(bool resume)
{
    if (ScreenClockOverlay::instance()->isVisible()) {
        qDebug() << "[ScreenBlanker] hideClockOverlayIfVisible: visible=" << true << " resume=" << resume;
        ScreenClockOverlay::instance()->hideClock(resume);
    } else {
        qDebug() << "[ScreenBlanker] hideClockOverlayIfVisible: overlay not visible";
    }
}

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

        // 等级文字（0-10 级）
        const int level = qBound(0, qRound(m_percent / 10.0), 10);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, barBot + 4, r.width(), textH),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(level));
    }

private:
    int     m_percent;
    QTimer *m_hideTimer;
};

// 异步读取当前音量百分比
static void scheduleVolumeRead(VolumeOverlay *overlay) {
    const QVariant currentVolume = qApp->property("appVolumeLevel");
    const int level = currentVolume.isValid()
        ? qBound(0, currentVolume.toInt(), 10)
        : QSettings().value("sound/volumeLevel", 10).toInt();
    overlay->showVolume(qBound(0, level, 10) * 10);
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
        if ((t == QEvent::MouseButtonPress || t == QEvent::TouchBegin) && ScreenClockOverlay::instance()->isVisible()) {
            ScreenClockOverlay::instance()->hideClock();
            return true;  // 消耗事件，不让底层界面误操作
        }
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
            QKeyEvent *ke = static_cast<QKeyEvent *>(event);
            const unsigned int sc = ke->nativeScanCode();
            const int k = ke->key();
            const bool isPowerKey = (sc == 116u || sc == 142u
                || k == Qt::Key_PowerOff || k == Qt::Key_Sleep
                || k == Qt::Key_WakeUp   || k == Qt::Key_PowerDown);
            if (ScreenBlanker::instance()->isBlanked()) {
                if (isPowerKey) {
                    return true;  // 消耗事件，亮屏由 InputNotifier::toggle() 完成
                }
                ScreenBlanker::instance()->unblank();
                return true;
            }
            if (isPowerKey) {
                return true;  // 消耗电源键 Qt 事件，避免 overlay/当前窗口收到重复按键
            }
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
                AppSignals::changeVolume(+1, nullptr);
                scheduleVolumeRead(m_overlay);
                return true;
            case Qt::Key_VolumeDown:
                qDebug() << "[GlobalKey] => VolumeDown";
                AppSignals::changeVolume(-1, nullptr);
                scheduleVolumeRead(m_overlay);
                return true;
            case Qt::Key_VolumeMute:
                qDebug() << "[GlobalKey] => Mute";
                AppSignals::toggleMute(nullptr);
                scheduleVolumeRead(m_overlay);
                return true;
            case Qt::Key_Menu:
                qDebug() << "[GlobalKey] => Menu";
                {
                    QWidget *w = QApplication::activeWindow();
                    if (!w) {
                        for (QWidget *tw : QApplication::topLevelWidgets()) {
                            if (tw->isVisible() && tw->isWindow()) { w = tw; break; }
                        }
                    }
                    if (w) {
                        QApplication::postEvent(w,
                            new QKeyEvent(QEvent::KeyPress, Qt::Key_HomePage, Qt::NoModifier));
                        QApplication::postEvent(w,
                            new QKeyEvent(QEvent::KeyRelease, Qt::Key_HomePage, Qt::NoModifier));
                    }
                }
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

static QStringList findInputEventDevices()
{
    QStringList devices;
    for (int i = 0; i <= 3; ++i) {
        const QString path = QStringLiteral("/dev/input/event%1").arg(i);
        if (QFileInfo(path).exists()) {
            devices.append(path);
        }
    }
    return devices;
}

static DrivingImageWindow *findDrivingImageWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *drive = qobject_cast<DrivingImageWindow *>(widget)) {
            return drive;
        }
    }
    return nullptr;
}

static MainWindow *findMainWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *main = qobject_cast<MainWindow *>(widget)) {
            return main;
        }
    }
    return nullptr;
}

static PhoneWindow *findPhoneWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *phone = qobject_cast<PhoneWindow *>(widget)) {
            return phone;
        }
    }
    return nullptr;
}

static bool handlePhoneKeyPress()
{
    if (PhoneWindow *phone = findPhoneWindow()) {
        return phone->handlePhoneKeyPress();
    }
    return false;
}

static bool handleEndKeyPress()
{
    if (PhoneWindow *phone = findPhoneWindow()) {
        return phone->handleEndKeyPress();
    }
    return false;
}

static MusicPlayerWindow *findMusicPlayerWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *music = qobject_cast<MusicPlayerWindow *>(widget)) {
            return music;
        }
    }
    return nullptr;
}

static RadioWindow *findRadioWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *radio = qobject_cast<RadioWindow *>(widget)) {
            return radio;
        }
    }
    return nullptr;
}

static VideoListWindow *findVideoListWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *video = qobject_cast<VideoListWindow *>(widget)) {
            return video;
        }
    }
    return nullptr;
}

static bool handleMenuKeyPress()
{
    if (PhoneWindow *phone = findPhoneWindow()) {
        if (phone->isVisible()) {
            return false;
        }
    }

    MainWindow *main = findMainWindow();
    if (!main) {
        return false;
    }

    QWidget *current = nullptr;
    if (auto *music = findMusicPlayerWindow()) {
        if (music->isVisible()) current = music;
    }
    if (!current) {
        if (auto *radio = findRadioWindow()) {
            if (radio->isVisible()) current = radio;
        }
    }
    if (!current) {
        if (auto *video = findVideoListWindow()) {
            if (video->isVisible()) current = video;
        }
    }
    if (!current) {
        if (main->isVisible()) {
            current = main;
        } else {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible()) continue;
                if (qobject_cast<PhoneWindow *>(widget)) continue;
                if (widget == main) continue;
                current = widget;
                break;
            }
        }
    }

    if (current && current != main) {
        current->hide();
    }

    if (qobject_cast<RadioWindow *>(current) || current == main) {
        qDebug() << "[InputNotifier] KEY_MENU => switch from radio/main to music";
        QMetaObject::invokeMethod(main, "onMusicUSBClicked", Qt::QueuedConnection);
        return true;
    }
    if (qobject_cast<MusicPlayerWindow *>(current)) {
        qDebug() << "[InputNotifier] KEY_MENU => switch from music to video";
        QMetaObject::invokeMethod(main, "onVideoListClicked", Qt::QueuedConnection);
        return true;
    }
    if (qobject_cast<VideoListWindow *>(current)) {
        qDebug() << "[InputNotifier] KEY_MENU => switch from video to radio";
        QMetaObject::invokeMethod(main, "onRadioClicked", Qt::QueuedConnection);
        return true;
    }

    qDebug() << "[InputNotifier] KEY_MENU => fallback to radio";
    QMetaObject::invokeMethod(main, "onRadioClicked", Qt::QueuedConnection);
    return true;
}

static bool routeMediaKeyToBackground(int qtKey)
{
    if (qtKey != Qt::Key_MediaPrevious && qtKey != Qt::Key_MediaNext) {
        return false;
    }

    QWidget *activeWindow = QApplication::activeWindow();
    if (!activeWindow) {
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (tw->isVisible() && tw->isWindow()) { activeWindow = tw; break; }
        }
    }
    if (activeWindow && qobject_cast<VideoPlayWindow *>(activeWindow)) {
        return false;
    }

    MusicPlayerWindow *music = findMusicPlayerWindow();
    RadioWindow *radio = findRadioWindow();
    MainWindow *main = findMainWindow();
    MediaManager::AudioSource currentAudioSource = MediaManager::AudioSource::None;
    if (main && main->mediaManager()) {
        currentAudioSource = main->mediaManager()->currentAudioSource();
    }

    QWidget *target = nullptr;

    if (music && music->isVisible()) {
        target = music;
    } else if (radio && radio->isVisible()) {
        target = radio;
    } else if (currentAudioSource == MediaManager::AudioSource::Radio && radio) {
        target = radio;
    } else if (radio && radio->isAudioActive()) {
        target = radio;
    } else if (music && music->isPlaying()) {
        target = music;
    }

    if (!target) {
        return false;
    }

    qDebug() << "[InputNotifier] routeMediaKeyToBackground =>" << qtKey << "target=" << target;
    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier));
    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier));
    return true;
}

static void activateDrivingImageMode(int mode)
{
    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        drive->setDrivingMode(mode);
        if (!drive->isVisible()) {
            drive->show();
        }
        drive->raise();
        drive->activateWindow();
        return;
    }

    if (MainWindow *main = findMainWindow()) {
        QMetaObject::invokeMethod(main, "onDrivingImageClicked", Qt::DirectConnection);
        if (DrivingImageWindow *drive = findDrivingImageWindow()) {
            drive->setDrivingMode(mode);
        }
    }
}

static void deactivateDrivingImageMode(int mode)
{
    if (DrivingImageWindow *drive = findDrivingImageWindow()) {
        if (drive->isVisible() && drive->drivingMode() == mode) {
            drive->close();
        }
    }
}

int main(int argc, char *argv[]) {
    // eglfs 平台下 Qt 不自动扫描 /dev/input，需在 QApplication 构造前设置。
    // 如果环境变量未设置，则选择第一个支持硬件按键的 event 设备。
    if (qgetenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS").isEmpty()) {
        const QStringList keyboardDevices = findInputEventDevices();
        if (!keyboardDevices.isEmpty()) {
            qputenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS", keyboardDevices.first().toLocal8Bit());
            qDebug() << "QT_QPA_EVDEV_KEYBOARD_PARAMETERS set to" << keyboardDevices.first();
        } else {
            qWarning() << "No keyboard input device found; fallback to /dev/input/event3";
            qputenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS", "/dev/input/event3");
        }
    }

    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // 设备 buildroot 默认 LANG=C，强制 UTF-8 避免中文文件名乱码
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() != DeviceDetect::DEVICE_TYPE_PC) {
        app.setOverrideCursor(Qt::BlankCursor);  // 触控设备隐藏鼠标指针
    }
    {
        QSettings settings;
        const bool init24h = settings.value("display/clock24h", false).toBool();
        app.setProperty("appClock24h", init24h);
        QString screenClockMode = settings.value("display/screenClockMode", "digital").toString();
        if (screenClockMode != QLatin1String("analog")) {
            screenClockMode = QLatin1String("digital");
        }
        app.setProperty("appScreenClockMode", screenClockMode);
    }
    app.setProperty("appSoundMode", QStringLiteral("立体声"));  // 默认声场模式
    T507SdkBridge::setSoundMode(QStringLiteral("立体声"));  // 应用默认声场到 TM2313
    app.setQuitOnLastWindowClosed(false);
    configureApplicationFont(app);

    // 全局硬件键监听（音量键 + 诊断日志）
    auto *volumeOverlay = new VolumeOverlay();
    app.installEventFilter(new GlobalKeyFilter(volumeOverlay, &app));

    // Qt 5.12 evdevkeyboard 默认 keymap 里没有 KEY_HOMEPAGE(172) 和 KEY_BACK(158)，
    // 直接用额外的 fd + QSocketNotifier 读 event* 来补全这两个键。
    // 与 evdevkeyboard 共用同一设备不冲突（内核允许多个 reader）。
    {
        const QString qtKeyboardDevice = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_KEYBOARD_PARAMETERS"));
        QStringList keyboardDevices = findInputEventDevices();
        if (keyboardDevices.isEmpty()) {
            qWarning() << "[InputNotifier] no keyboard input device found; fallback to /dev/input/event3";
            keyboardDevices.append("/dev/input/event3");
        }

        for (const QString &kbDev : keyboardDevices) {
            int kbFd = ::open(kbDev.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (kbFd < 0) {
                qWarning() << "[InputNotifier] failed to open" << kbDev;
                continue;
            }
            qDebug() << "[InputNotifier] opened" << kbDev;
            auto *notifier = new QSocketNotifier(kbFd, QSocketNotifier::Read, &app);
            QObject::connect(notifier, &QSocketNotifier::activated, &app, [kbFd, kbDev, qtKeyboardDevice]() {
                struct input_event ev;
                while (::read(kbFd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                    if (ev.type != EV_KEY || ev.value != 1) continue; // 只处理 key-down
                    if (kbDev == qtKeyboardDevice &&
                        (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN || ev.code == KEY_MUTE)) {
                        continue; // 避免 Qt 已经从该设备收到同一音量/静音按键，导致双重处理
                    }
                    int qtKey = 0;
                    switch (ev.code) {
                    case KEY_HOMEPAGE:      qtKey = Qt::Key_HomePage; break;
                    case KEY_HOME:          qtKey = Qt::Key_HomePage; break;
                    case KEY_BACK:          qtKey = Qt::Key_Back;     break;
                    case KEY_MENU:
                        if (handleMenuKeyPress()) {
                            continue;
                        }
                        qtKey = Qt::Key_Menu;
                        break;
                    case KEY_VOLUMEUP:
                        qtKey = Qt::Key_VolumeUp;
                        break;
                    case KEY_VOLUMEDOWN:
                        qtKey = Qt::Key_VolumeDown;
                        break;
                    case KEY_MUTE:          qtKey = Qt::Key_VolumeMute; break;
                    case KEY_PLAYPAUSE:     qtKey = Qt::Key_MediaTogglePlayPause; break;
                    case KEY_PREVIOUSSONG:
                        qtKey = Qt::Key_MediaPrevious;
                        if (routeMediaKeyToBackground(qtKey)) {
                            continue;
                        }
                        break;
                    case KEY_NEXTSONG:
                        qtKey = Qt::Key_MediaNext;
                        if (routeMediaKeyToBackground(qtKey)) {
                            continue;
                        }
                        break;
                    case KEY_PHONE:
                        if (handlePhoneKeyPress()) {
                            continue;
                        }
                        qtKey = Qt::Key_Phone;
                        break;
                    case KEY_END:
                        qDebug() << "[InputNotifier] ev.code=" << ev.code << "KEY_END => hangup";
                        if (handleEndKeyPress()) {
                            continue;
                        }
                        qtKey = Qt::Key_End;
                        break;
                    case KEY_A:
                        qDebug() << "[InputNotifier] ev.code=30 KEY_A => driving reverse mode";
                        activateDrivingImageMode(270);
                        continue;
                    case KEY_B:
                        qDebug() << "[InputNotifier] ev.code=78 KEY_B => exit reverse mode";
                        deactivateDrivingImageMode(270);
                        continue;
                    case KEY_C:
                        qDebug() << "[InputNotifier] ev.code=46 KEY_C => enter left-turn mode";
                        activateDrivingImageMode(271);
                        continue;
                    case KEY_D:
                        qDebug() << "[InputNotifier] ev.code=32 KEY_D => exit left-turn mode";
                        deactivateDrivingImageMode(271);
                        continue;
                    case KEY_K:
                        qDebug() << "[InputNotifier] ev.code=37 KEY_K => enter right-turn mode";
                        activateDrivingImageMode(272);
                        continue;
                    case KEY_L:
                        qDebug() << "[InputNotifier] ev.code=38 KEY_L => exit right-turn mode";
                        deactivateDrivingImageMode(272);
                        continue;
                    case KEY_M:
                        qDebug() << "[InputNotifier] ev.code=50 KEY_M => enter illumination mode";
                        activateDrivingImageMode(180);
                        continue;
                    case KEY_N:
                        qDebug() << "[InputNotifier] ev.code=49 KEY_N => exit illumination mode";
                        deactivateDrivingImageMode(180);
                        continue;
                    case KEY_SLEEP:
                        qDebug() << "[InputNotifier] ev.code=142 KEY_SLEEP => blank screen";
                        ScreenClockOverlay::instance()->hideClock(false);
                        ScreenBlanker::instance()->blank();
                        break;
                    case KEY_POWER:
                        if (ScreenBlanker::instance()->isBlanked()) {
                            qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => unblank screen";
                            ScreenBlanker::instance()->unblank();
                        } else {
                            bool closedDrivingImage = false;
                            for (QWidget *widget : QApplication::topLevelWidgets()) {
                                if (auto *drive = qobject_cast<DrivingImageWindow *>(widget)) {
                                    if (drive->isVisible()) {
                                        qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => close driving image";
                                        drive->close();
                                        closedDrivingImage = true;
                                    }
                                }
                            }
                            if (closedDrivingImage) {
                                qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => schedule clock overlay after driving image close";
                                QTimer::singleShot(0, []() {
                                    ScreenClockOverlay::instance()->showClock();
                                });
                            } else {
                                qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => toggle clock overlay";
                                ScreenClockOverlay::instance()->toggle();
                            }
                        }
                        break;
                    default: break;
                    }
                    if (qtKey == 0) continue;
                    qDebug() << "[InputNotifier] ev.code=" << ev.code << "=> qtKey=" << qtKey;
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
        }
    }
    // 检测设备信息
    qDebug() << "========== CarDesk Application Started ==========";
    qDebug() << "Device Type:" << device.getDeviceTypeString();
    qDebug() << "Platform:" << device.getPlatform();
    qDebug() << "Architecture:" << device.getArchitecture();
    qDebug() << "Screen Resolution:" << device.getScreenWidth() 
             << "x" << device.getScreenHeight();
    qDebug() << "Touch Device:" << (device.isTouchDevice() ? "Yes" : "No");
    qDebug() << "==============================================";
    
    // 启动时从配置读取音量等级并应用到 TM2313 / 系统音量
    {
        QSettings settings;
        const int savedLevel = qBound(0, settings.value("sound/volumeLevel", 10).toInt(), 10);
        AppSignals::setVolumeLevel(savedLevel, nullptr);
        app.setProperty("appVolumeLevel", savedLevel);
    }

    MainWindow window;
    window.show();
    
    return app.exec();
}
