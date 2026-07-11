#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QFileInfo>
#include <QTextCodec>
#include <QTimer>
#include <QDialog>
#include <QSettings>
#include <QSet>
#include <QTouchEvent>
#include <QTime>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QSocketNotifier>
#include <csignal>
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
#include "mcuserialreader.h"
#include "automotivedriving.h"
#include "appsettings.h"
#include "ahdmanager.h"
#include "tfcarddetect.h"
#include "processguard.h"
#include "applog.h"
#include "touchclicksound.h"

// ── 背光控制（POWER 键关/亮屏，SLEEP 键关机，具体 dispdbg 操作在 backlight.cpp）─
static MainWindow *findMainWindow();
static DrivingImageWindow *findDrivingImageWindow();

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
        if (DrivingImageWindow *drive = findDrivingImageWindow()) {
            if (drive->isVisible()) {
                qDebug() << "closing driving image before showing clock";
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
            // eglfs 平台 hide() 后底层窗口不会自动重绘，补发 update() 触发刷新
            QTimer::singleShot(0, this, []() {
                for (QWidget *tw : QApplication::topLevelWidgets()) {
                    if (tw->isVisible() && tw->isWindow()) {
                        tw->update();
                    }
                }
            });
            if (resume) {
                if (MainWindow *main = findMainWindow()) {
                    if (main->mediaManager()) {
                        QTimer::singleShot(0, this, [main]() {
                            if (main->mediaManager()) {
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

        const QPixmap bgPixmap(":/images/background.png");
        if (!bgPixmap.isNull()) {
            p.drawPixmap(rect(), bgPixmap.scaled(rect().size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        } else {
            p.fillRect(rect(), Qt::black);
        }

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

protected:
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

class ScreenBlanker : public ScreenClockOverlay {
public:
    static ScreenBlanker *instance() {
        static ScreenBlanker s;
        return &s;
    }
    bool isBlanked() const { return m_blanked; }

    void blank() {
        if (m_blanked) return;
        qDebug() << "blank: hiding clock overlay and pausing playback";
        if (MainWindow *main = findMainWindow()) {
            if (main->mediaManager()) {
                QTimer::singleShot(0, this, [main]() {
                    qDebug() << "blank: executing deferred pausePlaybackForInterruption";
                    main->mediaManager()->pausePlaybackForInterruption();
                });
            }
        }
        // 保存当前亮度（来自 Backlight 缓存或 sysfs）
        m_savedBrightness = Backlight::get();
        qDebug() << "blank: saved brightness=" << m_savedBrightness;
        m_blanked = true;
        Backlight::set(0);
        if (!isVisible())
            showClock();

    }
    void unblank() {
        if (!m_blanked) return;

        if (isVisible())
            hideClock(false);

        const int brightness = m_savedBrightness;
        QTimer::singleShot(50, this, [this, brightness]() {
            if (!m_blanked) {
                return;
            }
            m_blanked = false;
            Backlight::set(brightness);

            if (MainWindow *main = findMainWindow()) {
                if (main->mediaManager()) {
                    QTimer::singleShot(0, this, [main]() {
                        qDebug() << "unblank: executing deferred resumePlaybackAfterInterruption";
                        main->mediaManager()->resumePlaybackAfterInterruption();
                    });
                }
            }
        });
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

static bool shouldSuppressTouchClickSound()
{
    auto isAudioWindow = [](QWidget *w) {
        return w && (qobject_cast<MusicPlayerWindow *>(w)
                     || qobject_cast<RadioWindow *>(w)
                     || qobject_cast<VideoListWindow *>(w)
                     || qobject_cast<VideoPlayWindow *>(w)
                     || qobject_cast<PhoneWindow *>(w));
    };

    QWidget *activeWindow = QApplication::activeWindow();
    if (!activeWindow) {
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (tw->isVisible() && tw->isWindow()) {
                activeWindow = tw;
                break;
            }
        }
    }
    if (isAudioWindow(activeWindow)) {
        return true;
    }

    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (!widget->isVisible()) {
            continue;
        }
        if (isAudioWindow(widget)) {
            return true;
        }
    }

    // 媒体仍在播（含后台音乐）时不抢 ALSA；暂停态回主界面会 release，此处不再误判。
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *music = qobject_cast<MusicPlayerWindow *>(widget)) {
            if (music->isPlaying()) {
                return true;
            }
        }
        if (auto *radio = qobject_cast<RadioWindow *>(widget)) {
            if (radio->isAudioActive()) {
                return true;
            }
        }
        if (auto *video = qobject_cast<VideoPlayWindow *>(widget)) {
            if (video->isPlaying()) {
                return true;
            }
        }
    }

    return false;
}

static bool widgetSuppressesTouchClickSound(QObject *obj)
{
    while (obj) {
        if (obj->property("suppressTouchClickSound").toBool())
            return true;
        obj = obj->parent();
    }
    return false;
}

static bool shouldSkipTouchClickSound(QObject *watched, QEvent *event)
{
    if (widgetSuppressesTouchClickSound(watched)) {
        return true;
    }
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (!widget) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            widget = QApplication::widgetAt(me->globalPos());
        } else if (event->type() == QEvent::TouchBegin) {
            auto *te = static_cast<QTouchEvent *>(event);
            if (!te->touchPoints().isEmpty()) {
                widget = QApplication::widgetAt(te->touchPoints().first().screenPos().toPoint());
            }
        }
    }
    return widget && widgetSuppressesTouchClickSound(widget);
}

static bool isClickableWidget(QObject *watched, QEvent *event)
{
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (!widget) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            widget = QApplication::widgetAt(me->globalPos());
        } else if (event->type() == QEvent::TouchBegin) {
            auto *te = static_cast<QTouchEvent *>(event);
            if (!te->touchPoints().isEmpty()) {
                widget = QApplication::widgetAt(te->touchPoints().first().screenPos().toPoint());
            }
        }
    }
    while (widget) {
        if (qobject_cast<QAbstractButton *>(widget)
                || qobject_cast<QAbstractItemView *>(widget)) {
            return true;
        }
        widget = widget->parentWidget();
    }
    return false;
}

// 同一触摸点 id 在未收到 TouchEnd 前不应再次播点屏音（应对异常重复 TouchBegin）。
static QSet<int> s_touchClickSoundActiveIds;

static bool playTouchClickSound() {
    const int level = qApp->property("appTouchSoundLevel").toInt();
    if (level <= 0) {
        return false;
    }
    if (TouchClickSound::isBusy()) {
        return false;
    }
    if (shouldSuppressTouchClickSound()) {
        return false;
    }
    const bool ok = TouchClickSound::play(level);
    if (!ok) {
        qWarning() << "[ClickSound] play failed level=" << level;
    }
    return ok;
}

/** 点屏音已在专用音频线程播放，这里直接请求即可，不阻塞当前按键/触摸处理。 */
static void scheduleTouchClickSound()
{
    playTouchClickSound();
}

/** 左侧触屏键：开关屏 / 返回 / HOME / 音量加减 */
static bool isSidePanelClickKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_HomePage:
    case Qt::Key_Back:
    case Qt::Key_VolumeUp:
    case Qt::Key_VolumeDown:
    case Qt::Key_Sleep:
    case Qt::Key_PowerOff:
        return true;
    default:
        return false;
    }
}

static bool s_debugMode = false;

// 挂在 QApplication 上，能拦截所有窗口的 KeyPress，不依赖窗口焦点
class GlobalKeyFilter : public QObject {
public:
    explicit GlobalKeyFilter(VolumeOverlay *overlay, QObject *parent = nullptr)
        : QObject(parent)
        , m_overlay(overlay)
    {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // 任意按键或触摸 → 完成亮屏
        const QEvent::Type t = event->type();
        if (t == QEvent::TouchEnd || t == QEvent::TouchCancel) {
            const auto *te = static_cast<const QTouchEvent *>(event);
            for (const QTouchEvent::TouchPoint &tp : te->touchPoints()) {
                if (t == QEvent::TouchCancel || tp.state() == Qt::TouchPointReleased) {
                    s_touchClickSoundActiveIds.remove(tp.id());
                }
            }
        }
        if (t == QEvent::MouseButtonPress || t == QEvent::TouchBegin) {
            QWidget *hitForSound = nullptr;
            if (t == QEvent::TouchBegin) {
                const auto *te = static_cast<const QTouchEvent *>(event);
                if (!te->touchPoints().isEmpty()) {
                    hitForSound = QApplication::widgetAt(
                        te->touchPoints().constFirst().screenPos().toPoint());
                }
            } else {
                const auto *me = static_cast<const QMouseEvent *>(event);
                hitForSound = QApplication::widgetAt(me->globalPos());
            }
            const bool touchDevice = DeviceDetect::instance().isTouchDevice();
            const bool isTouchEvent = (t == QEvent::TouchBegin);
            // 用触点处 widgetAt 判断「是否点在可点控件上」，与事件实际投递给父还是子无关；
            // 同一次按压若有多条 TouchBegin，靠触摸点 id 只播一次。
            const bool shouldPlaySound = hitForSound
                && isClickableWidget(hitForSound, event)
                && !shouldSkipTouchClickSound(watched, event)
                && ((touchDevice && isTouchEvent) || (!touchDevice && !isTouchEvent));
            if (shouldPlaySound) {
                if (isTouchEvent) {
                    const auto *te = static_cast<const QTouchEvent *>(event);
                    const int tid = te->touchPoints().isEmpty()
                        ? -1 : te->touchPoints().constFirst().id();
                    const bool duplicateTouchBegin = (tid >= 0
                        && s_touchClickSoundActiveIds.contains(tid));
                    if (!duplicateTouchBegin) {
                        if (tid >= 0) {
                            s_touchClickSoundActiveIds.insert(tid);
                        }
                        scheduleTouchClickSound();
                    }
                } else {
                    scheduleTouchClickSound();
                }
            }
        }
        if (t == QEvent::KeyPress) {
            QKeyEvent *ke = static_cast<QKeyEvent *>(event);
            qDebug() << "[GlobalKey] type=KeyPress"
                     << "key=" << ke->key()
                     << "nativeScanCode=" << ke->nativeScanCode()
                     << "nativeVirtualKey=" << ke->nativeVirtualKey()
                     << "watched=" << watched->metaObject()->className();

            const int key = ke->key();

            // 左侧触屏键点屏音（音乐/视频在播或媒体窗前台时由 shouldSuppress 屏蔽）
            if (!ke->isAutoRepeat() && isSidePanelClickKey(key)) {
                scheduleTouchClickSound();
            }

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

            if (!ScreenBlanker::instance()->isBlanked() && !ScreenBlanker::instance()->isVisible()) {
                switch (key) {
                case Qt::Key_VolumeUp:
                    qDebug() << "[GlobalKey] => VolumeUp";
                    if (s_debugMode) {
                        automotiveSetRightTurnSignal(true);
                    } else {
                        AppSignals::changeVolume(+1);
                        scheduleVolumeRead(m_overlay);
                    }
                    return true;
                case Qt::Key_VolumeDown:
                    qDebug() << "[GlobalKey] => VolumeDown";
                    if (s_debugMode) {
                        automotiveSetLeftTurnSignal(true);
                    } else {
                        AppSignals::changeVolume(-1);
                        scheduleVolumeRead(m_overlay);
                    }
                    return true;
                case Qt::Key_VolumeMute:
                    qDebug() << "[GlobalKey] => Mute";
                    AppSignals::toggleMute();
                    scheduleVolumeRead(m_overlay);
                    return true;
                case Qt::Key_Menu:
                    qDebug() << "[GlobalKey] => Menu";
                    // 倒车/转向中禁止 MODE；正常切换由 InputNotifier::handleMenuKeyPress 处理
                    if (automotiveIsTurnOrReverseActive()) {
                        qDebug() << "[GlobalKey] Menu ignored: turn/reverse active";
                        return true;
                    }
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

static VideoPlayWindow *findVideoPlayWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *video = qobject_cast<VideoPlayWindow *>(widget)) {
            return video;
        }
    }
    return nullptr;
}

static bool handleMenuKeyPress()
{
    // 倒车/转向中禁止 MODE 切换音乐/视频/收音机（吞掉按键，不转发）
    if (automotiveIsTurnOrReverseActive()) {
        qDebug() << "[InputNotifier] KEY_MENU ignored: turn/reverse active";
        return true;
    }

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
    if (auto *videoPlay = findVideoPlayWindow()) {
        if (videoPlay->isVisible()) current = videoPlay;
    }
    if (!current) {
        if (auto *music = findMusicPlayerWindow()) {
            if (music->isVisible()) current = music;
        }
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

    // 切换链：(video play / video list) → radio → music → video list → radio → ...
    const char *nextSlot = "onRadioClicked";
    if (qobject_cast<RadioWindow *>(current) || current == main) {
        nextSlot = "onMusicUSBClicked";
        qDebug() << "[InputNotifier] KEY_MENU => switch from radio/main to music";
    } else if (qobject_cast<MusicPlayerWindow *>(current)) {
        nextSlot = "onVideoListClicked";
        qDebug() << "[InputNotifier] KEY_MENU => switch from music to video";
    } else if (qobject_cast<VideoListWindow *>(current)
               || qobject_cast<VideoPlayWindow *>(current)) {
        nextSlot = "onRadioClicked";
        qDebug() << "[InputNotifier] KEY_MENU => switch from video to radio";
    } else {
        nextSlot = "onRadioClicked";
        qDebug() << "[InputNotifier] KEY_MENU => fallback to radio";
    }

    // MENU 切换语义 = "先标记被中断 + 走 HOME 释放路径"：
    //   - HOME 用户按：musicHide 时仍 isPlaying()，preserve 分支不 release，音乐继续后台播。
    //   - MENU 切换：在 postEvent(HOME) 之前同步调一次 pauseForInterruption()。它把
    //     m_pausedForInterruption=true 设上并停止播放，hideEvent 看到 !isPlaying() 走 capture+release，
    //     下一个播放器拿到 ALSA；再次进入音乐界面时凭 m_pausedForInterruption 自动 resume。
    //   pauseForInterruption() 内部已区分 BT/SDK/QMP 三种路径并自行保存位置，且已做幂等保护
    //   （未在播时不会把已设上的 m_pausedForInterruption 清回 false），所以稍后 MainWindow 的
    //   onVideoListClicked / prepareForRadioAudio 再次调它也不会丢失中断态。
    if (auto *musicNow = qobject_cast<MusicPlayerWindow *>(current)) {
        musicNow->pauseForInterruption();
    }

    // 模拟用户按 HOME：把 HOME 键事件投递给当前窗口，让它走自己已有的 HOME 路径
    //（保存进度 / 释放播放器 / emit requestReturnToMain / hide），状态机与按 HOME 完全一致。
    // 然后再投递下一个界面对应的按钮 slot 完成切换。
    // 用 postEvent 而非 sendEvent：本工程运行环境的 libQt5Core.so 未导出
    //   QCoreApplication::sendEvent 符号（Qt 5.15+ 改为 inline），用 sendEvent 会启动失败。
    if (current && current != main) {
        QApplication::postEvent(current,
            new QKeyEvent(QEvent::KeyPress, Qt::Key_HomePage, Qt::NoModifier));
        QApplication::postEvent(current,
            new QKeyEvent(QEvent::KeyRelease, Qt::Key_HomePage, Qt::NoModifier));
    }

    QMetaObject::invokeMethod(main, nextSlot, Qt::QueuedConnection);
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

int main(int argc, char *argv[]) {
    installAppLogHandler();

    QStringList rawArgs;
    rawArgs.reserve(qMax(0, argc - 1));
    for (int i = 1; i < argc; ++i) {
        rawArgs.append(QString::fromLocal8Bit(argv[i]));
    }
    for (const QString &arg : rawArgs) {
        const QString lowerArg = arg.toLower();
        if (lowerArg == QStringLiteral("debug")
            || lowerArg == QStringLiteral("--debug")
            || lowerArg == QStringLiteral("-debug")
            || lowerArg == QStringLiteral("/debug")) {
            s_debugMode = true;
            qDebug() << "Debug enabled";
            break;
        }
    }

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

    QString instanceLockError;
    if (!ProcessGuard::tryAcquireInstanceLock(&instanceLockError)) {
        qCritical().noquote() << instanceLockError;
        return 1;
    }

    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    logBuildInfo();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { ProcessGuard::releaseInstanceLock(); });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { TouchClickSound::shutdown(); });
    if (s_debugMode) {
        AppSettings::setDebugMode(true);
    }
#ifdef CAR_DESK_USE_T507_SDK
    AhdManager::globalInit();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { AhdManager::globalCleanup(); });
    // First Ctrl+C quits; second forces exit if SDK teardown blocks (e.g. after TF unplug).
    std::signal(SIGINT, +[](int sig) {
        static volatile sig_atomic_t hits = 0;
        if (++hits >= 2) {
            AhdManager::globalCleanup();
            ProcessGuard::releaseInstanceLock();
            _exit(128 + sig);
        }
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
        }
    });
    std::signal(SIGTERM, +[](int sig) {
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
        } else {
            _exit(128 + sig);
        }
    });
#endif
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
        const int touchClickLevel = qBound(0, settings.value("sound/touchClickLevel", 1).toInt(), 2);
        app.setProperty("appTouchSoundLevel", touchClickLevel);
    }
    AppSettings::syncAppPropertiesFromSettings();
    app.setProperty("appSoundMode", QStringLiteral("立体声"));  // 默认声场模式
    T507SdkBridge::setSoundMode(QStringLiteral("立体声"));  // 应用默认声场到 TM2313
    app.setQuitOnLastWindowClosed(false);
    configureApplicationFont(app);

    // 全局硬件键监听（音量键 + 诊断日志）
    auto *volumeOverlay = new VolumeOverlay();
    QTimer::singleShot(500, &app, [volumeOverlay]() {
        volumeOverlay->move(-2000, -2000);
        volumeOverlay->show();
        volumeOverlay->raise();
        QTimer::singleShot(300, volumeOverlay, [volumeOverlay]() {
            volumeOverlay->hide();
        });
    });
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
                    qDebug() << "[InputNotifier] ev.code=" << ev.code;

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
                        qDebug() << "KEY_END => hangup";
                        if (handleEndKeyPress()) {
                            continue;
                        }
                        qtKey = Qt::Key_End;
                        break;
                    case KEY_A:
                        qDebug() << "KEY_A => enter reverse (backup ON)";
                        automotiveSetBackupSignal(true);
                        continue;
                    case KEY_B:
                        qDebug() << "KEY_B => exit reverse (backup OFF)";
                        automotiveSetBackupSignal(false);
                        continue;
                    case KEY_C:
                        qDebug() << "KEY_C => left turn signal ON";
                        automotiveSetLeftTurnSignal(true);
                        continue;
                    case KEY_D:
                        qDebug() << "KEY_D => left turn signal OFF";
                        automotiveSetLeftTurnSignal(false);
                        continue;
                    case KEY_K:
                        qDebug() << "KEY_K => right turn signal ON";
                        automotiveSetRightTurnSignal(true);
                        continue;
                    case KEY_L:
                        qDebug() << "KEY_L => right turn signal OFF";
                        automotiveSetRightTurnSignal(false);
                        continue;
                    case KEY_M:
                        qDebug() << "KEY_M => cabin illumination ON";
                        automotiveSetIllumination(true);
                        continue;
                    case KEY_N:
                        qDebug() << "KEY_N => cabin illumination OFF";
                        automotiveSetIllumination(false);
                        continue;
                    case KEY_SLEEP:
                        qDebug() << "KEY_SLEEP => blank screen";
                        // 不投递 KeyPress，在此补点屏音
                        scheduleTouchClickSound();
                        if (DrivingImageWindow *drive = findDrivingImageWindow()) {
                            if (!drive->isVisible()) {
                                ScreenBlanker::instance()->blank();
                            }
                        }
                        break;
                    case KEY_POWER:
                        // 不投递 KeyPress，在此补点屏音
                        scheduleTouchClickSound();
                        if (ScreenBlanker::instance()->isBlanked()) {
                            qDebug() << "[InputNotifier] ev.code=116 KEY_POWER => unblank screen";
                            ScreenBlanker::instance()->unblank();
                        } else {
                            if (DrivingImageWindow *drive = findDrivingImageWindow()) {
                                if (!drive->isVisible()) {
                                    ScreenBlanker::instance()->toggle();
                                }
                            }
                        }
                        break;
                    default: break;
                    }
                    if (qtKey == 0) continue;

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
    
    // 启动时从配置读取音量、亮度并应用
    {
        QSettings settings;
        const int savedLevel = qBound(0, settings.value("sound/volumeLevel", 10).toInt(), 10);
        AppSignals::setVolumeLevel(savedLevel);
        app.setProperty("appVolumeLevel", savedLevel);

        if (settings.contains(QStringLiteral("brightness/mode"))) {
            const int mode = settings.value(QStringLiteral("brightness/mode")).toInt();
            const int daySlider = qBound(0, settings.value(QStringLiteral("brightness/day"), 100).toInt(), 100);
            const int nightSlider = qBound(0, settings.value(QStringLiteral("brightness/night"), 20).toInt(), 100);
            int sliderVal = daySlider;
            if (mode == 1) {
                sliderVal = nightSlider;
            } else if (mode == 2) {
                // 自动：大灯开=夜晚，大灯关=白天（启动时大灯状态通常为关）
                sliderVal = automotiveIlluminationOn() ? nightSlider : daySlider;
            }
            const int bl = Backlight::sliderToBacklight(sliderVal);
            Backlight::set(bl);
            qDebug() << "[Boot] restore brightness mode=" << mode << "slider=" << sliderVal << "bl=" << bl;
        }
    }

    TfCardMonitor::instance()->start();

    MainWindow window;
    window.show();

    // ── TXRX CAN 数据读取（左转/右转/倒车行车摄像 + MCU 时间同步）────────────────
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        McuSerialReader *txrxReader = McuSerialReader::ensureShared(&app);
        txrxReader->open(QStringLiteral("/dev/ttyS2"));

        // LC/OEL 由 McuSerialReader::emitLcIfChanged 每帧调用 automotiveSyncCanSignals

        QObject::connect(AppSignals::instance(), &AppSignals::vehicleSpeedChanged,
                         &app, [](float speedKmh) {
            automotiveUpdateVehicleSpeed(speedKmh);
        });

        // TD：程序启动后仅同步一次；须在不播视频时收到才执行（date -s 会打断 XPlayer）
        QObject::connect(txrxReader, &McuSerialReader::tdReceived,
                         &app, [&window](int year, int month, int day, int hour, int min) {
            static bool canTdTimeSynced = false;
            if (canTdTimeSynced) {
                return;
            }

            if (MediaManager *mm = window.mediaManager()) {
                if (VideoListWindow *list = mm->videoListWindow()) {
                    if (QWidget *pw = list->videoPlayWindow()) {
                        if (pw->isVisible()) {
                            const auto *video = qobject_cast<const VideoPlayWindow *>(pw);
                            if (video && video->isPlaying()) {
                                return;
                            }
                        }
                    }
                }
            }

            canTdTimeSynced = true;
            const QString dateStr = QString("%1-%2-%3 %4:%5:00")
                .arg(year)
                .arg(month, 2, 10, QChar('0'))
                .arg(day,   2, 10, QChar('0'))
                .arg(hour,  2, 10, QChar('0'))
                .arg(min,   2, 10, QChar('0'));
            qDebug() << "[TXRX] sync system time (once after boot):" << dateStr;
            const QString cmd = QStringLiteral("date -s \"%1\" && hwclock -w").arg(dateStr);
            QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
        });
    }

    return app.exec();
}
