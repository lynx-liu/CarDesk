#include "radiowindow.h"
#include "devicedetect.h"
#include "topbarwidget.h"
#include "t507sdkbridge.h"
#include "appsignals.h"

#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QProcess>
#include <QDateTime>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QLineEdit>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QDebug>
#include <QSettings>

// ── V4L2 ─────────────────────────────────────────────────────────────────────
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

// V4L2 频率单位：1/16 kHz。
// FM: 87.5 MHz = 87500 kHz => 87500*16 = 1400000
// AM:  522  kHz =   522 kHz =>   522*16 =    8352
static inline quint32 mhzToV4l2(double mhz) { return static_cast<quint32>(mhz * 1000.0 * 16.0); }
static inline quint32 khzToV4l2(double khz) { return static_cast<quint32>(khz * 16.0); }
static inline double  v4l2ToMhz(quint32 v)  { return v / 16000.0; }
static inline double  v4l2ToKhz(quint32 v)  { return v / 16.0; }
// ─────────────────────────────────────────────────────────────────────────────

// ── 电台列表 Delegate ──────────────────────────────────────────────────────────
// CSS: .radio_list_con ul li { 212×212; bg:radio_list_up/down.png }
//      span:first  { font-size:48; line-height:48; margin-top:52; text-align:center }
//      span:last   { font-size:36; line-height:36; margin-top:24; text-align:center }
class RadioListDelegate : public QStyledItemDelegate {
    bool m_fm;
public:
    explicit RadioListDelegate(bool fm, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_fm(fm) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const QRect r = option.rect;   // 212×212

        // 背景：hover / selected → down；normal → up
        const bool active = (option.state & QStyle::State_MouseOver) ||
                            (option.state & QStyle::State_Selected);
        const QPixmap bg(active ? QStringLiteral(":/images/radio_list_down.png")
                                : QStringLiteral(":/images/radio_list_up.png"));
        if (!bg.isNull())
            painter->drawPixmap(r, bg);

        painter->setPen(Qt::white);

        // 频率文字：font-size:48px; margin-top:52; line-height:48
        const QString freq = index.data(Qt::UserRole).toString();
        QFont f = painter->font();
        f.setPixelSize(48);
        painter->setFont(f);
        painter->drawText(QRect(r.x(), r.y() + 52, r.width(), 48),
                          Qt::AlignCenter, freq);

        // 单位文字：font-size:36px; margin-top:24 (after freq span)
        const QString unit = m_fm ? QStringLiteral("MHz") : QStringLiteral("kHz");
        f.setPixelSize(36);
        painter->setFont(f);
        painter->drawText(QRect(r.x(), r.y() + 52 + 48 + 24, r.width(), 36),
                          Qt::AlignCenter, unit);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    { return QSize(212, 212); }
};
// ─────────────────────────────────────────────────────────────────────────────

// ── 底部电台条 Delegate ────────────────────────────────────────────────────────
// CSS: .radio_play_list li { width:150; height:118; bg:radio_play_list_up/down.png;
//                             font-size:36px; line-height:118px; text-align:center }
//      li:hover,.radio_on { background:down; color:#00FAFF }
class StationStripDelegate : public QStyledItemDelegate {
public:
    explicit StationStripDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const QRect r = option.rect;  // 150×118

        const bool active = (option.state & QStyle::State_Selected) ||
                            (option.state & QStyle::State_MouseOver);
        const QPixmap bg(active
            ? QStringLiteral(":/images/radio_play_list_down.png")
            : QStringLiteral(":/images/radio_play_list_up.png"));
        if (!bg.isNull())
            painter->drawPixmap(r, bg);

        // font-size:36px; line-height:118px → AlignVCenter
        QFont f = painter->font();
        f.setPixelSize(36);
        painter->setFont(f);
        painter->setPen(active ? QColor(0x00, 0xFA, 0xFF) : Qt::white);
        painter->drawText(r, Qt::AlignHCenter | Qt::AlignVCenter,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    { return QSize(150, 118); }
};
// ─────────────────────────────────────────────────────────────────────────────

RadioWindow::RadioWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_fd(-1)
    , m_freqLabel(nullptr)
    , m_unitLabel(nullptr)
    , m_barLabel(nullptr)
    , m_barScrollArea(nullptr)
    , m_scaleLabel(nullptr)
    , m_fmTabBtn(nullptr)
    , m_amTabBtn(nullptr)
    , m_searchBtn(nullptr)
    , m_favoriteBtn(nullptr)
    , m_scanBtn(nullptr)
    , m_stationList(nullptr)
    , m_stereoLabel(nullptr)
    , m_fmStations({"88.7", "90.6", "91.2", "92.5", "95.9", "96.3", "97.7", "99.8", "101.1", "106.6"})
    , m_amStations({"554", "639", "756", "855", "937", "955", "981", "1008", "1143", "1323"})
    , m_isFM(true)
    , m_frequency(95.9)
    , m_tunerCapLow(true)
    , m_tunerIndex(0)
    , m_favorite(false)
    , m_scanMode(false)
    , m_seekUpward(true)
    , m_seekStartFreq(95.9)
    , m_seekStepCount(0)
    , m_scanTimer(new QTimer(this))
    , m_barDragging(false)
    , m_barDragStartX(0)
    , m_barDragStartScroll(0) {

    m_scanTimer->setInterval(200);
    connect(m_scanTimer, &QTimer::timeout, this, &RadioWindow::onScanTick);
    setWindowTitle("收音机");
    setFixedSize(1280, 720);

    const DeviceDetect &device = DeviceDetect::instance();
    if (device.getDeviceType() == DeviceDetect::DEVICE_TYPE_CARUNIT) {
        setWindowState(Qt::WindowFullScreen);
    } else if (QApplication::primaryScreen()) {
        move(QApplication::primaryScreen()->geometry().center() - rect().center());
    }

    setupUI();
    
    // 加载用户收藏（如果有）
    {
        QSettings settings;
        m_fmFavorites = settings.value("radio/fmFavorites").toStringList();
        m_amFavorites = settings.value("radio/amFavorites").toStringList();
    }

    // 尝试打开硬件设备
    if (openDevice()) {
        // tea685x 通过频率值自动切换 FM/AM，无需 VIDIOC_S_TUNER
        // 读回硬件当前频率；验证是否在当前频段有效范围内
        quint32 v = getFrequencyHz();
        if (v > 0) {
            double freq = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);
            const double minFreq = m_isFM ? 87.0 : 522.0;
            const double maxFreq = m_isFM ? 108.0 : 1710.0;
            if (freq >= minFreq && freq <= maxFreq) {
                m_frequency = freq;   // 驱动频率与当前频段一致，直接使用
            } else {
                // 驱动处于另一频段，强制写入目标频段默认频率（驱动自动切换）
                setFrequencyHz(m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency));
            }
        } else {
            // 硬件未返回频率，主动写入当前默认频率
            setFrequencyHz(m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency));
        }
    }

    updateFrequencyView();

    // 进入收音机界面时自动切到收音机声道
    T507SdkBridge::setAudioSource(true);
    setMute(false);
    // 开始播放后延迟300ms读取立体声状态
    QTimer::singleShot(300, this, [this]() { updateTunerStatus(); });
}

RadioWindow::~RadioWindow()
{
    stopScan();
    closeDevice();
    // 退出收音机：将 TM2313 功放输入切回媒体声道（IN2 = SoC DAC）
    T507SdkBridge::setAudioSource(false);
}

void RadioWindow::closeEvent(QCloseEvent *event) {
    stopScan();
    closeDevice();
    // 退出收音机：将 TM2313 功放输入切回媒体声道（IN2 = SoC DAC）
    T507SdkBridge::setAudioSource(false);
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

// ──────────────────────────────────────────────────────────────────────────────
// 鼠标拖拽事件过滤器：使频率条 QScrollArea 可手动拖动
// ──────────────────────────────────────────────────────────────────────────────
bool RadioWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (m_barScrollArea && obj == m_barScrollArea->viewport()) {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (me->button() == Qt::LeftButton) {
                m_barDragging = true;
                m_barDragStartX = me->x();
                m_barDragStartScroll = m_barScrollArea->horizontalScrollBar()->value();
                m_barScrollArea->viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        case QEvent::MouseMove:
            if (m_barDragging) {
                // 向左拖 → 滚动条增大 → 频率升高（与 HTML 拖动一致）
                const int delta = m_barDragStartX - me->x();
                QScrollBar *sb = m_barScrollArea->horizontalScrollBar();
                sb->setValue(qBound(0, m_barDragStartScroll + delta, sb->maximum()));
                // 实时更新频率显示（不向驱动写入，避免过多 ioctl）
                const int barWidth   = m_isFM ? 2160 : 2480;
                const double minFreq = m_isFM ? 87.0  : 522.0;
                const double maxFreq = m_isFM ? 108.0 : 1710.0;
                const double pix     = sb->value() + 347.0;
                double freq = minFreq + pix / barWidth * (maxFreq - minFreq);
                freq = qBound(minFreq, freq, maxFreq);
                if (m_freqLabel)
                    m_freqLabel->setText(m_isFM ? QString::number(freq, 'f', 1)
                                                : QString::number(freq, 'f', 0));
                m_frequency = freq;
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_barDragging && me->button() == Qt::LeftButton) {
                m_barDragging = false;
                m_barScrollArea->viewport()->setCursor(Qt::OpenHandCursor);
                // 松手时向驱动写入最终频率
                if (m_fd >= 0) {
                    quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
                    setFrequencyHz(fhz);
                    QTimer::singleShot(300, this, [this]() { updateTunerStatus(); });
                }
                updateFrequencyView();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ══════════════════════════════════════════════════════════════════════════════
// V4L2 底层接口
// ══════════════════════════════════════════════════════════════════════════════

bool RadioWindow::openDevice()
{
    if (m_fd >= 0) return true;
    m_fd = ::open("/dev/radio0", O_RDWR);
    if (m_fd < 0) {
        qWarning() << "RadioWindow: cannot open /dev/radio0:" << strerror(errno);
        return false;
    }
    // 验证是 radio 类型
    struct v4l2_capability cap;
    if (::ioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        qWarning() << "RadioWindow: VIDIOC_QUERYCAP failed";
        ::close(m_fd); m_fd = -1;
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_RADIO)) {
        qWarning() << "RadioWindow: device is not a radio";
        ::close(m_fd); m_fd = -1;
        return false;
    }
    qDebug() << "RadioWindow: opened /dev/radio0, driver=" << reinterpret_cast<const char*>(cap.driver);

    // 查询调谐器能力 —— 确定频率单位是否为 62.5 Hz（LOW）还是 62.5 kHz
    struct v4l2_tuner tuner;
    memset(&tuner, 0, sizeof(tuner));
    tuner.index = 0;
    tuner.type  = V4L2_TUNER_RADIO;
    if (::ioctl(m_fd, VIDIOC_G_TUNER, &tuner) == 0) {
        m_tunerCapLow = (tuner.capability & V4L2_TUNER_CAP_LOW) != 0;
        qDebug() << "RadioWindow: tuner" << reinterpret_cast<const char*>(tuner.name)
                 << "cap_low=" << m_tunerCapLow
                 << "range=" << tuner.rangelow << "-" << tuner.rangehigh;
    } else {
        qWarning() << "RadioWindow: VIDIOC_G_TUNER failed, assuming cap_low=true";
        m_tunerCapLow = true;
    }

    // ── 枚举驱动所有支持的 V4L2 控制，便于诊断频段切换机制 ──────────────
    {
        struct v4l2_queryctrl qc;
        memset(&qc, 0, sizeof(qc));
        qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
        qDebug() << "RadioWindow: enumerating V4L2 controls...";
        while (::ioctl(m_fd, VIDIOC_QUERYCTRL, &qc) == 0) {
            if (!(qc.flags & V4L2_CTRL_FLAG_DISABLED)) {
                qDebug("RadioWindow:   ctrl id=0x%08X name='%s' type=%d min=%d max=%d def=%d",
                       qc.id, qc.name, qc.type, qc.minimum, qc.maximum, qc.default_value);
            }
            qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        }
        // 单独探测 V4L2_CID_BAND_STOP_FILTER（某些 BSP 驱动将其复用为 AM/FM 频段选择器）
        struct v4l2_queryctrl bsf;
        memset(&bsf, 0, sizeof(bsf));
        bsf.id = V4L2_CID_BAND_STOP_FILTER;   // V4L2_CID_BASE + 33
        if (::ioctl(m_fd, VIDIOC_QUERYCTRL, &bsf) == 0)
            qDebug("RadioWindow: V4L2_CID_BAND_STOP_FILTER(0x%08X) min=%d max=%d def=%d",
                   bsf.id, bsf.minimum, bsf.maximum, bsf.default_value);
        else
            qDebug() << "RadioWindow: V4L2_CID_BAND_STOP_FILTER not supported";
    }

    // ── 探测额外 tuner 索引（部分驱动将 AM 暴露为 tuner[1]）────────────────────
    for (int ti = 1; ti <= 3; ++ti) {
        struct v4l2_tuner t2;
        memset(&t2, 0, sizeof(t2));
        t2.index = static_cast<__u32>(ti);
        t2.type  = V4L2_TUNER_RADIO;
        if (::ioctl(m_fd, VIDIOC_G_TUNER, &t2) != 0) break;
        qDebug("RadioWindow: tuner[%d] '%s' cap=0x%X range=%u-%u",
               ti, reinterpret_cast<const char*>(t2.name),
               t2.capability, t2.rangelow, t2.rangehigh);
    }

    // ── 探测频段列表（VIDIOC_ENUM_FREQ_BANDS，Linux ≥ 3.14）────────────────────
    for (int bi = 0; bi <= 7; ++bi) {
        struct v4l2_frequency_band fb;
        memset(&fb, 0, sizeof(fb));
        fb.tuner = 0;
        fb.type  = V4L2_TUNER_RADIO;
        fb.index = static_cast<__u32>(bi);
        if (::ioctl(m_fd, VIDIOC_ENUM_FREQ_BANDS, &fb) != 0) {
            if (!bi) qDebug() << "RadioWindow: VIDIOC_ENUM_FREQ_BANDS not supported";
            break;
        }
        qDebug("RadioWindow: freq_band[%d] mod=0x%X range=%u-%u cap=0x%X",
               bi, fb.modulation, fb.rangelow, fb.rangehigh, fb.capability);
    }

    // ── 探测音频输入（VIDIOC_ENUMAUDIO，某些驱动用音频输入选择频段）──────────────
    for (int ai = 0; ai <= 3; ++ai) {
        struct v4l2_audio audio;
        memset(&audio, 0, sizeof(audio));
        audio.index = static_cast<__u32>(ai);
        if (::ioctl(m_fd, VIDIOC_ENUMAUDIO, &audio) != 0) {
            if (!ai) qDebug() << "RadioWindow: VIDIOC_ENUMAUDIO not supported";
            break;
        }
        qDebug("RadioWindow: audio_input[%d] '%s' cap=0x%X mode=0x%X",
               ai, reinterpret_cast<const char*>(audio.name),
               audio.capability, audio.mode);
    }

    // ── sysfs 设备路径（BSP 驱动可能在此暴露 band 切换属性文件）──────────────────
    {
        char syspath[512] = {};
        ssize_t n = readlink("/sys/class/video4linux/radio0", syspath, sizeof(syspath)-1);
        if (n > 0)
            qDebug("RadioWindow: sysfs radio0 -> %s", syspath);
    }

    return true;
}

void RadioWindow::closeDevice()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool RadioWindow::setFrequencyHz(quint32 freqHz)
{
    if (m_fd < 0) return false;
    // freqHz 内部单位为 1/16 kHz（V4L2 LOW = 62.5 Hz）
    // 若驱动不支持 CAP_LOW，需转换为 62.5 kHz 单位（÷ 1000）
    struct v4l2_frequency vf;
    memset(&vf, 0, sizeof(vf));
    vf.tuner    = static_cast<__u32>(m_tunerIndex);
    vf.type     = V4L2_TUNER_RADIO;
    vf.frequency = m_tunerCapLow ? freqHz : (freqHz / 1000u);
    if (::ioctl(m_fd, VIDIOC_S_FREQUENCY, &vf) < 0) {
        qWarning() << "RadioWindow: VIDIOC_S_FREQUENCY failed (value=" << vf.frequency
                   << "):" << strerror(errno);
        return false;
    }
    return true;
}

quint32 RadioWindow::getFrequencyHz() const
{
    if (m_fd < 0) return 0;
    struct v4l2_frequency vf;
    memset(&vf, 0, sizeof(vf));
    vf.tuner = static_cast<__u32>(m_tunerIndex);
    vf.type  = V4L2_TUNER_RADIO;
    if (::ioctl(m_fd, VIDIOC_G_FREQUENCY, &vf) < 0) {
        qWarning() << "RadioWindow: VIDIOC_G_FREQUENCY failed:" << strerror(errno);
        return 0;
    }
    // 统一将驱动返回值转换为内部单位（LOW = 1/16 kHz）
    return m_tunerCapLow ? vf.frequency : (vf.frequency * 1000u);
}

bool RadioWindow::setMute(bool mute)
{
    if (m_fd < 0) return false;
    struct v4l2_control ctrl;
    ctrl.id    = V4L2_CID_AUDIO_MUTE;
    ctrl.value = mute ? 1 : 0;
    if (::ioctl(m_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        qWarning() << "RadioWindow: VIDIOC_S_CTRL MUTE failed:" << strerror(errno);
        return false;
    }
    return true;
}

void RadioWindow::updateTunerStatus()
{
    if (m_fd < 0 || !m_isFM) {
        if (m_stereoLabel) m_stereoLabel->setVisible(false);
        return;
    }
    struct v4l2_tuner tuner;
    memset(&tuner, 0, sizeof(tuner));
    tuner.index = static_cast<__u32>(m_tunerIndex);
    tuner.type  = V4L2_TUNER_RADIO;
    if (::ioctl(m_fd, VIDIOC_G_TUNER, &tuner) == 0) {
        const bool isStereo = (tuner.rxsubchans & V4L2_TUNER_SUB_STEREO) != 0;
        if (m_stereoLabel) m_stereoLabel->setVisible(isStereo);
    }
}

bool RadioWindow::startAutoSeek(bool upward)
{
    if (m_fd < 0) return false;
    setMute(true);           // 开始搜台时静音
    m_scanTimer->stop();      // 停止旧的搜台（如果有）
    m_seekUpward    = upward;
    m_seekStartFreq = m_frequency;
    m_seekStepCount = 0;
    m_scanTimer->start();
    return true;
}

void RadioWindow::stopScan()
{
    setMute(false);
    if (m_scanTimer) m_scanTimer->stop();
    m_scanMode      = false;
    m_seekStepCount = 0;
}

void RadioWindow::setupUI() {
    QWidget *central = new QWidget(this);
    central->setStyleSheet("background-image:url(:/images/inside_background.png);background-repeat:no-repeat;");
    setCentralWidget(central);

    // ── 顶部栏 (0,0,1280,82) ──────────────────────────────────────────
    QWidget *topBar = new QWidget(central);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background-image:url(:/images/topbar.png);");

    QPushButton *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 17, 48, 48);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);}"
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}");
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this]{ emit requestReturnToMain(); close(); });

    QLabel *titleLbl = new QLabel("收音机", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

    setupTopStatusIcons(topBar);

    // ── Tab: FM(480,100,160×66) AM(640,100,160×66) ──────────────────────
    // CSS .tab { width:320px; height:66px; margin:18px auto 0; } => x=(1280-320)/2=480, y=82+18=100
    m_fmTabBtn = new QPushButton("FM", central);
    m_fmTabBtn->setGeometry(480, 100, 160, 66);
    m_fmTabBtn->setCursor(Qt::PointingHandCursor);
    m_fmTabBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_fmTabBtn, &QPushButton::clicked, this, &RadioWindow::onSwitchFM);

    m_amTabBtn = new QPushButton("AM", central);
    m_amTabBtn->setGeometry(640, 100, 160, 66);
    m_amTabBtn->setCursor(Qt::PointingHandCursor);
    m_amTabBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_amTabBtn, &QPushButton::clicked, this, &RadioWindow::onSwitchAM);

    // ── 频率显示区 (y:186, h:120) ─────────────────────────────────────
    // CSS .radio_con { margin-top:20 } => top = 166+20 = 186
    // CSS .radio_detail { height:120px; position:relative }
    // CSS .radio_detail>span (STEREO): absolute, left:240px
    // CSS .radio_detail h2: font-size:120px, text-align:center

    // 频率数字容器，全宽水平居中
    QWidget *freqRow = new QWidget(central);
    freqRow->setGeometry(0, 186, 1280, 120);
    freqRow->setStyleSheet("background:transparent;");
    {
        QHBoxLayout *lay = new QHBoxLayout(freqRow);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addStretch();
        m_freqLabel = new QLabel(freqRow);
        m_freqLabel->setStyleSheet("color:#fff;font-size:108px;background:transparent;");
        m_freqLabel->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        lay->addWidget(m_freqLabel);
        lay->addSpacing(21);   // CSS margin-left:21px on unit span
        m_unitLabel = new QLabel("MHz", freqRow);
        m_unitLabel->setStyleSheet("color:#fff;font-size:48px;background:transparent;");
        m_unitLabel->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
        lay->addWidget(m_unitLabel);
        lay->addStretch();
    }

    // STEREO 标签：absolute left:240 在 freqRow 之上覆盖
    m_stereoLabel = new QLabel("STEREO", central);
    m_stereoLabel->setGeometry(240, 186, 220, 120);
    m_stereoLabel->setStyleSheet("color:#00FAFF;font-size:36px;background:transparent;");
    m_stereoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_stereoLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_stereoLabel->setVisible(false);  // 默认隐藏，播放后由硬件状态更新

    // ── 频率条区域 ────────────────────────────────────────────────────────────
    // CSS .radio_control { width:1056; margin:20px auto }
    //   => x=(1280-1056)/2=112, y=186+120+20=326
    // CSS .radio_pro: flex, space-between
    //   prev span: 120×120 at (112,326)
    //   bar div:   720×106, margin-top:30 => (232,356)
    //   next span: 120×120 at (952,326)
    //   ::before mask: left:166 in radio_pro => global x=112+166=278, y=326
    //   ::after  mask: right:166 in radio_pro => global x=112+(1056-166-64)=938, y=326
    //   .radio_mark: left:50% of radio_pro=528 => global x=112+528-4=636
    //               bottom:43 in radio_pro(h=136) => global y=326+(136-43-85)=334

    QPushButton *prev = new QPushButton(central);
    prev->setGeometry(112, 326, 120, 120);
    prev->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_searchpre_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_searchpre_down.png);}");
    prev->setCursor(Qt::PointingHandCursor);
    prev->setFocusPolicy(Qt::NoFocus);
    connect(prev, &QPushButton::clicked, this, &RadioWindow::onPrev);

    // barArea: CSS .radio_pro div { width:720; height:106; margin-top:30 }
    //  space-between: prev(120)+gap48+div(720)+gap48+next(120)=1056
    //  div absolute x = 112+120+48 = 280, y=326+30=356
    m_barScrollArea = new QScrollArea(central);
    m_barScrollArea->setGeometry(280, 356, 720, 106);
    m_barScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_barScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_barScrollArea->setFrameShape(QFrame::NoFrame);
    m_barScrollArea->setStyleSheet(
        "QScrollArea,QScrollArea>QWidget{background:transparent;border:none;}"
        "QScrollBar:horizontal{height:0px;background:transparent;}"
        "QScrollBar:vertical{width:0px;background:transparent;}");
    m_barScrollArea->viewport()->setStyleSheet("background:transparent;");
    m_barLabel = new QLabel();
    m_barLabel->setStyleSheet("background:transparent;");
    m_barScrollArea->setWidget(m_barLabel);
    m_barScrollArea->setWidgetResizable(false);
    // 安装鼠标拖拽事件过滤器，使频率条可手动拖动（模拟 HTML overflow-x:auto 效果）
    m_barScrollArea->viewport()->installEventFilter(this);
    m_barScrollArea->viewport()->setCursor(Qt::OpenHandCursor);

    QPushButton *nextBtn = new QPushButton(central);
    nextBtn->setGeometry(1048, 326, 120, 120);
    nextBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_searchnext_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_searchnext_down.png);}");
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setFocusPolicy(Qt::NoFocus);
    connect(nextBtn, &QPushButton::clicked, this, &RadioWindow::onNext);

    // 左遂照： CSS ::before left:166 in radio_pro(x=112) => global x=278
    QLabel *leftMask = new QLabel(central);
    leftMask->setGeometry(278, 356, 64, 106);
    leftMask->setPixmap(QPixmap(":/images/pict_radio_barmask_left.png"));
    leftMask->setStyleSheet("background:transparent;");
    leftMask->setAttribute(Qt::WA_TransparentForMouseEvents);

    // 右遒照： CSS ::after right:166 => left = 112+(1056-166-64) = 938
    QLabel *rightMask = new QLabel(central);
    rightMask->setGeometry(938, 356, 64, 106);
    rightMask->setPixmap(QPixmap(":/images/pict_radio_barmask_right.png"));
    rightMask->setStyleSheet("background:transparent;");
    rightMask->setAttribute(Qt::WA_TransparentForMouseEvents);

    // 标尺针：居中于 radio_pro 50%处，HTML margin-left=-568 校准 => 全局 x=623(中心627)
    // scrollArea左=280, markerX=627-280=347
    QLabel *mark = new QLabel(central);
    mark->setGeometry(623, 334, 8, 85);
    mark->setPixmap(QPixmap(":/images/pict_radio_mark.png"));
    mark->setStyleSheet("background:transparent;");
    mark->setAttribute(Qt::WA_TransparentForMouseEvents);
    mark->raise();

    // ── 控制按钟区 (238,472,804×94) ───────────────────────────────────────
    // CSS .radio_btn { width:804; margin:20px auto } 坐标文件实测 y=472
    // CSS justify-content:space-between 5个按鈕：总宽=60+60+84+60+60=324, 间距=(804-324)/4=120
    // x: list=0 search=180 play=360 fav=564 scan=744
    // y: 60px 按鈕 margin-top:12(+ul margin-top:10)=22; 84px play margin-top:0(+10)=10
    QWidget *btnRow = new QWidget(central);
    btnRow->setGeometry(238, 472, 804, 94);
    btnRow->setStyleSheet("background:transparent;");

    QPushButton *listBtn = new QPushButton(btnRow);
    listBtn->setGeometry(0, 22, 60, 60);
    listBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_list_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_list_down.png);}");
    listBtn->setCursor(Qt::PointingHandCursor);
    listBtn->setFocusPolicy(Qt::NoFocus);
    connect(listBtn, &QPushButton::clicked, this, &RadioWindow::onOpenListDialog);

    m_searchBtn = new QPushButton(btnRow);
    m_searchBtn->setGeometry(248, 22, 60, 60);
    m_searchBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_search_down.png);}");
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_searchBtn, &QPushButton::clicked, this, &RadioWindow::onSearch);

    m_favoriteBtn = new QPushButton(btnRow);
    m_favoriteBtn->setGeometry(496, 22, 60, 60);
    m_favoriteBtn->setCursor(Qt::PointingHandCursor);
    m_favoriteBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &RadioWindow::onToggleFavorite);

    m_scanBtn = new QPushButton(btnRow);
    m_scanBtn->setGeometry(744, 22, 60, 60);
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_scanBtn, &QPushButton::clicked, this, &RadioWindow::onToggleScan);

    // ── 电台列表 (81,582,1118×118) ─────────────────────────────────────────
    // CSS .radio_play_list { width:1118; margin:16px auto }
    //   => x=(1280-1118)/2=81, y=坐标文件实测 582, h=118
    m_stationList = new QListWidget(central);
    m_stationList->setGeometry(81, 582, 1118, 118);
    m_stationList->setFlow(QListView::LeftToRight);
    m_stationList->setWrapping(false);
    m_stationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_stationList->setGridSize(QSize(150, 118));
    m_stationList->setSpacing(0);
    m_stationList->setContentsMargins(0, 0, 0, 0);
    m_stationList->setMouseTracking(true);
    m_stationList->viewport()->setMouseTracking(true);
    m_stationList->setItemDelegate(new StationStripDelegate(m_stationList));
    m_stationList->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;padding:0;}"
        "QScrollBar:horizontal{height:0px;background:transparent;border:none;}"
        "QScrollBar:vertical{width:0px;background:transparent;border:none;}");
    rebuildStationStrip();

    connect(m_stationList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        m_frequency = item->text().toDouble();
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
        updateFrequencyView();
    });

    switchBand(true);
}

void RadioWindow::updateFrequencyView() {
    if (m_fmTabBtn && m_amTabBtn) {
        m_fmTabBtn->setChecked(m_isFM);
        m_amTabBtn->setChecked(!m_isFM);
        m_fmTabBtn->setStyleSheet(m_isFM
            ? "QPushButton{border:none;background:url(:/images/butt_tab_left_on.png);color:#fff;font-size:28px;}"
            : "QPushButton{border:none;background:url(:/images/butt_tab_left_down.png);color:#fff;font-size:28px;}");
        m_amTabBtn->setStyleSheet(!m_isFM
            ? "QPushButton{border:none;background:url(:/images/butt_tab_right_on.png);color:#fff;font-size:28px;}"
            : "QPushButton{border:none;background:url(:/images/butt_tab_right_down.png);color:#fff;font-size:28px;}");
    }

    if (m_freqLabel) {
        m_freqLabel->setText(m_isFM ? QString::number(m_frequency, 'f', 1)
                                    : QString::number(m_frequency, 'f', 0));
    }
    if (m_unitLabel) {
        m_unitLabel->setText(m_isFM ? "MHz" : "kHz");
    }
    // AM 始终单声道；FM 立体声由 updateTunerStatus / onScanTick 实时更新
    if (m_stereoLabel && !m_isFM) m_stereoLabel->setVisible(false);
    if (m_barScrollArea && m_barLabel) {
        const QString barPath = m_isFM ? QStringLiteral(":/images/pict_radio_fmbar.png")
                                       : QStringLiteral(":/images/pict_radio_ambar.png");
        const QPixmap barPixmap(barPath);
        if (!barPixmap.isNull()) {
            const int viewportWidth = 720;
            // markerX: HTML校准值。CSS left:50%=528px from radio_pro(x=112) => 全局x=640
            // HTML margin-left:-568 at 95.9MHz反推: pixel(95.9)=8.9/21*2160=915.4
            // markerX = 915.4 - 568 = 347; mark widget center = scrollArea_left + markerX = 280+347=627
            const int markerX = 347;
            const int barWidth = barPixmap.width();
            const double minFreq = m_isFM ? 87.0 : 522.0;
            const double maxFreq = m_isFM ? 108.0 : 1710.0;
            const double clamped = qBound(minFreq, m_frequency, maxFreq);
            const double ratio = (clamped - minFreq) / (maxFreq - minFreq);
            // scrollValue = 在条图中的偏移，使标记釅对准当前频率刻度
            const int scrollPos = static_cast<int>(ratio * barWidth) - markerX;
            const int maxScroll = barWidth - viewportWidth;
            m_barLabel->setPixmap(barPixmap);
            m_barLabel->setFixedSize(barWidth, 106);
            m_barScrollArea->horizontalScrollBar()->setRange(0, maxScroll);
            m_barScrollArea->horizontalScrollBar()->setValue(qBound(0, scrollPos, maxScroll));
        }
    }

    if (m_stationList) {
        const QString needle = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
        QList<QListWidgetItem *> items = m_stationList->findItems(needle, Qt::MatchExactly);
        if (!items.isEmpty()) {
            m_stationList->setCurrentItem(items.first());
            m_stationList->scrollToItem(items.first(), QAbstractItemView::PositionAtCenter);
        }
    }
    // 根据收藏列表实时计算收藏状态
    {
        const QString key = m_isFM ? QString::number(m_frequency, 'f', 1)
                                   : QString::number(qRound(m_frequency));
        const QStringList &favs = m_isFM ? m_fmFavorites : m_amFavorites;
        m_favorite = favs.contains(key);
    }
    if (m_favoriteBtn) {
        // m_favorite=true → 已收藏（_down状态常亮）；false → 未收藏（_up 普通，悬停显 _down）
        m_favoriteBtn->setStyleSheet(m_favorite
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_collection_down.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_collection_up.png);}"
              "QPushButton:hover{background-image:url(:/images/butt_music_collection_down.png);}");
    }
    if (m_scanBtn) {
        m_scanBtn->setStyleSheet(m_scanMode
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_scan_down.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_scan_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_scan_down.png);}"
        );
    }
}

void RadioWindow::onSwitchFM() {
    switchBand(true);
}

void RadioWindow::onSwitchAM() {
    switchBand(false);
}

void RadioWindow::onPrev() {
    if (m_fd >= 0) {
        startAutoSeek(false);
        return;
    }
    // 无硬件：手动步进（FM 0.1MHz / AM 9kHz）
    const double step = m_isFM ? 0.1 : 9.0;
    const double minFreq = m_isFM ? 87.0 : 522.0;
    const double maxFreq = m_isFM ? 108.0 : 1710.0;
    m_frequency = qBound(minFreq, m_frequency - step, maxFreq);
    updateFrequencyView();
}

void RadioWindow::onNext() {
    if (m_fd >= 0) {
        startAutoSeek(true);
        return;
    }
    // 无硬件：手动步进（FM 0.1MHz / AM 9kHz）
    const double step = m_isFM ? 0.1 : 9.0;
    const double minFreq = m_isFM ? 87.0 : 522.0;
    const double maxFreq = m_isFM ? 108.0 : 1710.0;
    m_frequency = qBound(minFreq, m_frequency + step, maxFreq);
    updateFrequencyView();
}

void RadioWindow::onToggleFavorite() {
    const QString key = m_isFM ? QString::number(m_frequency, 'f', 1)
                                : QString::number(qRound(m_frequency));
    QStringList &favs = m_isFM ? m_fmFavorites : m_amFavorites;
    if (favs.contains(key))
        favs.removeAll(key);
    else
        favs.append(key);
    // 保存收藏到本地设置
    {
        QSettings settings;
        settings.setValue("radio/fmFavorites", m_fmFavorites);
        settings.setValue("radio/amFavorites", m_amFavorites);
    }
    updateFrequencyView();
}

void RadioWindow::onToggleScan() {
    m_scanMode = !m_scanMode;
    if (m_scanMode) {
        // 开始连续自动扫台（用户空间逐频点）
        if (m_fd >= 0) startAutoSeek(true);
    } else {
        m_scanTimer->stop();
        m_seekStepCount = 0;
    }
    updateFrequencyView();
}

void RadioWindow::onScanTick() {
    const double step    = m_isFM ? 0.1   : 9.0;
    const double minFreq = m_isFM ? 87.0  : 522.0;
    const double maxFreq = m_isFM ? 108.0 : 1710.0;
    // FM: 21MHz/0.1=210步；AM: 1188kHz/9≈132步，各加2余量
    const int    maxSteps = m_isFM ? 212 : 134;

    // ① 先检测本次频率点的信号强度（设置后已沉待约200ms）
    if (m_seekStepCount > 0 && m_fd >= 0) {
        struct v4l2_tuner tuner;
        memset(&tuner, 0, sizeof(tuner));
        tuner.index = static_cast<__u32>(m_tunerIndex);
        tuner.type  = V4L2_TUNER_RADIO;
        if (::ioctl(m_fd, VIDIOC_G_TUNER, &tuner) == 0) {
            // 更新立体声标识
            const bool stereo = (tuner.rxsubchans & V4L2_TUNER_SUB_STEREO) != 0;
            if (m_stereoLabel) m_stereoLabel->setVisible(m_isFM && stereo);

            // tea685x signal: (raw_dBuV+20)*(0xffff/140)
            // 25dBuV ≈ (25+20)*468 = 21060；使用 20000 作为门限
            const quint32 threshold = m_isFM ? 20000u : 16000u;
            if (static_cast<quint32>(tuner.signal) > threshold) {
                // 找到电台！
                m_scanTimer->stop();
                updateFrequencyView();
                if (m_scanMode) {
                    // 连续扫台：停留1.5s后继续
                    QTimer::singleShot(1500, this, [this]() {
                        if (m_scanMode) {
                            m_seekStepCount = 0;
                            m_seekStartFreq = m_frequency;
                            m_scanTimer->start();
                        }
                    });
                }
                setMute(false);  // 停止搜台后取消静音
                return;
            }
        }
    }

    // ② 检测是否已绕一圈
    if (m_seekStepCount >= maxSteps) {
        m_scanTimer->stop();
        m_scanMode = false;
        updateFrequencyView();
        return;
    }

    // ③ 步进到下一个频率
    double nextFreq = m_frequency + (m_seekUpward ? step : -step);
    if (nextFreq > maxFreq + step * 0.5) nextFreq = minFreq;
    else if (nextFreq < minFreq - step * 0.5) nextFreq = maxFreq;

    m_frequency = nextFreq;
    m_seekStepCount++;

    if (m_fd >= 0) {
        const quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
    }
    // 实时刷新频率显示（不更新电台列表，避免频繁重建）
    if (m_freqLabel)
        m_freqLabel->setText(m_isFM ? QString::number(m_frequency, 'f', 1)
                                    : QString::number(m_frequency, 'f', 0));
}

void RadioWindow::onSearch() {
    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(1280, 720);
    dialog.setStyleSheet("QDialog{background-image:url(:/images/inside_background.png);}");

    // 顶部栏
    QWidget *topBar = new QWidget(&dialog);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background-image:url(:/images/topbar.png);");
    // HOME 按钮
    QPushButton *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 17, 48, 48);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);}" 
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}");
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this, &dialog]{ emit requestReturnToMain(); dialog.reject(); this->close(); });

    QLabel *titleLbl = new QLabel("收音机", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    setupTopStatusIcons(topBar);

    // 返回按钮 (60,103,60,60)
    QPushButton *backBtn = new QPushButton(&dialog);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_back_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_back_down.png);}");
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 输入框容器 (232,157,816,72)：用 QWidget 包裹，内含图标 + QLineEdit
    // CSS: border:1px #0068FF; padding-left:88; bg: butt_radiolist_search_up.png 24px center
    QWidget *inputWrap = new QWidget(&dialog);
    inputWrap->setGeometry(232, 157, 816, 72);
    inputWrap->setStyleSheet(
        "QWidget{border:1px solid #0068FF;background:#000;}");

    // 搜索图标覆盖在输入框左侧 24px 处，图标尺寸取 48×48 居中于高度 72
    QLabel *searchIcon = new QLabel(inputWrap);
    searchIcon->setGeometry(24, (72-48)/2, 48, 48);
    searchIcon->setPixmap(QPixmap(":/images/butt_radiolist_search_up.png").scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    searchIcon->setStyleSheet("border:none;background:transparent;");
    searchIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    QLineEdit *input = new QLineEdit(
        m_isFM ? QString::number(m_frequency, 'f', 1)
               : QString::number(m_frequency, 'f', 0), inputWrap);
    input->setGeometry(88, 1, 816-88-1, 70);
    input->setStyleSheet(
        "QLineEdit{"
        "  border:none; color:#fff; font-size:48px;"
        "  background:transparent;"
        "}");

    // 清零按钮 (976,169,48,48)
    QPushButton *clearBtn = new QPushButton(&dialog);
    clearBtn->setGeometry(976, 169, 48, 48);
    clearBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_del_all_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_search_del_all_down.png);}");
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, &dialog, [input]() { input->clear(); });

    // 输入验证规则：
    //   FM: 整数部分最多3位(87~108)，小数部分最多1位，总计最多5字符，如 "108.0"
    //   AM: 纯整数，最多4位(531~1602)，不允许小数点
    auto canInsert = [input, this](const QString &ch) -> bool {
        const QString cur = input->text();
        if (ch == ".") {
            if (!m_isFM) return false;           // AM 不允许小数点
            if (cur.contains('.')) return false;  // 已有小数点
            if (cur.isEmpty()) return false;      // 必须先输入整数位
            return true;
        }
        // 数字字符
        const bool hasDot = cur.contains('.');
        if (hasDot) {
            // 小数部分：FM 小数后最多1位
            const int dotIdx = cur.indexOf('.');
            if ((cur.length() - dotIdx) >= 2) return false;
        } else {
            // 整数部分：FM 最多3位，AM 最多4位
            const int maxInt = m_isFM ? 3 : 4;
            if (cur.length() >= maxInt) return false;
        }
        return true;
    };

    // 键盘布局：列 gap=8 → x偏移 0/206/412；行 gap=8 → y偏移 0/102/204/306
    // grid 起始 (232,237)，每键 198×94
    struct KeyDef { const char *label; int col; int row; };
    static const KeyDef keyDefs[] = {
        {"1",0,0},{"2",1,0},{"3",2,0},
        {"4",0,1},{"5",1,1},{"6",2,1},
        {"7",0,2},{"8",1,2},{"9",2,2},
        {".",0,3},{"0",1,3},{nullptr,2,3}
    };
    for (const auto &k : keyDefs) {
        int bx = 232 + k.col * 206;
        int by = 237 + k.row * 102;
        QPushButton *btn = new QPushButton(&dialog);
        btn->setGeometry(bx, by, 198, 94);
        btn->setCursor(Qt::PointingHandCursor);
        if (k.label) {
            const QString lbl = QString::fromUtf8(k.label);
            btn->setText(lbl);
            // AM 模式下 "." 键灰显禁用
            if (lbl == "." && !m_isFM) {
                btn->setEnabled(false);
                btn->setStyleSheet(
                    "QPushButton{border:1px solid #334466;color:#334466;"
                    "  font-size:48px;font-weight:700;background:transparent;}");
            } else {
                btn->setStyleSheet(
                    "QPushButton{border:1px solid #0068FF;color:#fff;"
                    "  font-size:48px;font-weight:700;background:transparent;}"
                    "QPushButton:pressed{border-color:#00FAFF;color:#00FAFF;}");
                connect(btn, &QPushButton::clicked, &dialog, [input, lbl, canInsert]() {
                    if (canInsert(lbl)) input->insert(lbl);
                });
            }
        } else {
            btn->setStyleSheet(
                "QPushButton{border:none;"
                "  background:url(:/images/butt_radio_search_del_up.png) no-repeat center center;}"
                "QPushButton:pressed{"
                "  background-image:url(:/images/butt_radio_search_del_down.png);}");
            connect(btn, &QPushButton::clicked, &dialog, [input]() {
                QString t = input->text();
                if (!t.isEmpty()) input->setText(t.left(t.size() - 1));
            });
        }
    }

    // 确认按钮 (850,237,198,400)
    QPushButton *confirm = new QPushButton("确认", &dialog);
    confirm->setGeometry(850, 237, 198, 400);
    confirm->setCursor(Qt::PointingHandCursor);
    // CSS .radio_search_keybord .radio_search_enter { background:#0068FF } :hover { background:#00FAFF }
    confirm->setStyleSheet(
        "QPushButton{border:none;background:#0068FF;"
        "  color:#fff;font-size:48px;font-weight:bold;}"
        "QPushButton:hover{background:#00FAFF;}");
    connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() == QDialog::Accepted) {
        bool ok = false;
        const double v = input->text().toDouble(&ok);
        if (!ok) return;
        m_frequency = m_isFM ? qBound(87.0, v, 108.0) : qBound(522.0, v, 1710.0);
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
        updateFrequencyView();
    }
}

void RadioWindow::onOpenListDialog() {
    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(1280, 720);
    dialog.setStyleSheet("QDialog{background-image:url(:/images/inside_background.png);}");

    // 顶部栏
    QWidget *topBar = new QWidget(&dialog);
    topBar->setGeometry(0, 0, 1280, 82);
    topBar->setStyleSheet("background-image:url(:/images/topbar.png);");
    // HOME 按钮
    QPushButton *homeBtn = new QPushButton(topBar);
    homeBtn->setGeometry(12, 17, 48, 48);
    homeBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/pict_home_up.png);}" 
        "QPushButton:hover{background-image:url(:/images/pict_home_down.png);}");
    homeBtn->setCursor(Qt::PointingHandCursor);
    connect(homeBtn, &QPushButton::clicked, this, [this, &dialog]{ emit requestReturnToMain(); dialog.reject(); this->close(); });

    QLabel *titleLbl = new QLabel("收音机", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    setupTopStatusIcons(topBar);

    // 返回按鈕：匹配 CSS .back { left:60; top:103; w:60; h:60 }
    QPushButton *backBtn = new QPushButton(&dialog);
    backBtn->setGeometry(60, 103, 60, 60);
    backBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_back_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_back_down.png);}");
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    // Tab (480,100,160×66)：favTab 默认选中（_on），localTab 未选中（_down），互斥切换
    auto styleTabOn  = [](bool left) {
        return QString("QPushButton{border:none;background:url(:/images/%1);color:#fff;font-size:28px;}")
            .arg(left ? "butt_tab_left_on.png" : "butt_tab_right_on.png");
    };
    auto styleTabOff = [](bool left) {
        return QString("QPushButton{border:none;background:url(:/images/%1);color:#fff;font-size:28px;}")
            .arg(left ? "butt_tab_left_down.png" : "butt_tab_right_down.png");
    };

    QPushButton *favTab = new QPushButton("我的收藏", &dialog);
    favTab->setGeometry(480, 100, 160, 66);
    favTab->setStyleSheet(styleTabOn(true));
    favTab->setCursor(Qt::PointingHandCursor);

    QPushButton *localTab = new QPushButton("本地电台", &dialog);
    localTab->setGeometry(640, 100, 160, 66);
    localTab->setStyleSheet(styleTabOff(false));
    localTab->setCursor(Qt::PointingHandCursor);

    // 电台网格（必须在 Tab connect 之前创建，供 lambda 捕获）
    // CSS .radio_list_con { width:1060; margin:16px auto }
    // 宽度用 1066（= 5×212 + 6px slack）避免 Qt viewport 舍入导致只显示 4 列
    QListWidget *list = new QListWidget(&dialog);
    list->setGeometry(107, 182, 1066, 424);
    list->setFrameShape(QFrame::NoFrame);
    list->setContentsMargins(0, 0, 0, 0);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Fixed);
    list->setMovement(QListView::Static);
    list->setWrapping(true);
    list->setGridSize(QSize(212, 212));
    list->setIconSize(QSize(1, 1));
    list->setSpacing(0);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setMouseTracking(true);
    list->viewport()->setMouseTracking(true);
    list->setAttribute(Qt::WA_Hover);
    list->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;padding:0;margin:0;}"
        "QListWidget::item{width:212px;height:212px;background:transparent;}"
        "QScrollBar:vertical,QScrollBar:horizontal{width:0;height:0;background:transparent;}");
    list->setItemDelegate(new RadioListDelegate(m_isFM, list));

    // ── Tab 切换逻辑：0=我的收藏  1=本地电台 ──────────────────────────────
    int currentTab = 0;
    QLabel *hint = nullptr;   // viewport 子 Widget，切换时必须先销毁
    auto refillList = [&]() {
        // 先销毁上一次的提示标签，再清空列表
        if (hint) { delete hint; hint = nullptr; }
        list->clear();
        // 我的收藏：用户通过 ♡ 按钮加入；本地电台：默认预置列表
        const QStringList &src = (currentTab == 0)
            ? (m_isFM ? m_fmFavorites : m_amFavorites)
            : (m_isFM ? m_fmStations  : m_amStations);
        for (const QString &s : src) {
            QListWidgetItem *it = new QListWidgetItem(list);
            it->setData(Qt::UserRole, s);
            it->setSizeHint(QSize(212, 212));
        }
        // 收藏为空时显示提示
        if (currentTab == 0 && src.isEmpty()) {
            hint = new QLabel(m_isFM ? "暂无收藏的 FM 电台\n\n在播放界面点击 ♡ 收藏当前频率"
                                     : "暂无收藏的 AM 电台\n\n在播放界面点击 ♡ 收藏当前频率",
                              list->viewport());
            hint->setStyleSheet("color:#aaa;font-size:28px;background:transparent;");
            hint->setAlignment(Qt::AlignCenter);
            hint->setGeometry(0, 50, 1066, 200);
            hint->show();
        }
    };

    connect(favTab, &QPushButton::clicked, &dialog, [&]() {
        currentTab = 0;
        favTab->setStyleSheet(styleTabOn(true));
        localTab->setStyleSheet(styleTabOff(false));
        refillList();
    });
    connect(localTab, &QPushButton::clicked, &dialog, [&]() {
        currentTab = 1;
        localTab->setStyleSheet(styleTabOn(false));
        favTab->setStyleSheet(styleTabOff(true));
        refillList();
    });

    refillList();   // 初始填充（我的收藏）

    // 搜索按钮：用 setIcon 使图标与文字紧挨在一起
    QPushButton *searchLinkBtn = new QPushButton("搜索", &dialog);
    searchLinkBtn->setGeometry(1040, 622, 200, 54);
    searchLinkBtn->setIcon(QIcon(":/images/butt_radiolist_search_up.png"));
    searchLinkBtn->setIconSize(QSize(48, 48));
    searchLinkBtn->setStyleSheet(
        "QPushButton{border:none;background:transparent;"
        "  color:#fff;font-size:36px;text-align:left;padding-left:4px;}"
        "QPushButton:hover{color:#00FAFF;}");
    searchLinkBtn->setCursor(Qt::PointingHandCursor);
    connect(searchLinkBtn, &QPushButton::clicked, &dialog, [&dialog, this]() {
        dialog.reject();
        onSearch();
    });

    // 单击电台即切换频率并关闭
    connect(list, &QListWidget::itemClicked, &dialog, [&, this](QListWidgetItem *item) {
        m_frequency = item->data(Qt::UserRole).toString().toDouble();
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
        updateFrequencyView();
        dialog.accept();
    });

    dialog.exec();
}

void RadioWindow::setupTopStatusIcons(QWidget *topBar) {
    auto *right = new TopBarRightWidget(topBar);
    right->setGeometry(1280 - 16 - TopBarRightWidget::preferredWidth(), 17,
                       TopBarRightWidget::preferredWidth(), 48);
}

void RadioWindow::rebuildStationStrip() {
    if (!m_stationList) {
        return;
    }
    m_stationList->clear();
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    for (const QString &s : stations) {
        QListWidgetItem *it = new QListWidgetItem(s, m_stationList);
        it->setSizeHint(QSize(150, 118));
        m_stationList->addItem(it);
    }
}

void RadioWindow::switchBand(bool fm) {
    stopScan();
    m_isFM = fm;
    m_frequency = m_isFM ? 95.9 : 937.0;

    if (m_fd >= 0) {
        // tea685x 驱动根据上次设置的频率锁定频段（FM 模式拒绝 AM 频率，反之亦然）
        m_tunerIndex = 0;  // 频段切换时先重置 tuner 索引为默认值
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        const quint32 driverFreq = m_tunerCapLow ? fhz : (fhz / 1000u);
        bool ok = false;

        // ── 方式 0：VIDIOC_S_TUNER + rangelow/rangehigh（tea685x 正确切频段方式）──
        // 驱动 vidioc_s_tuner 通过比较 rangelow/rangehigh 与 bands[] 表来选择频段：
        //   FM bands[0]: rangelow=8700*160=1392000  rangehigh=10800*160=1728000
        //   AM bands[1]: rangelow=522*16=8352        rangehigh=1710*16=27360
        {
            struct v4l2_tuner t;
            memset(&t, 0, sizeof(t));
            t.index     = 0;
            t.type      = V4L2_TUNER_RADIO;
            t.audmode   = V4L2_TUNER_MODE_STEREO;
            t.rangelow  = m_isFM ? 1392000u : 8352u;
            t.rangehigh = m_isFM ? 1728000u : 27360u;
            if (::ioctl(m_fd, VIDIOC_S_TUNER, &t) == 0) {
                qDebug() << "RadioWindow: band switch via VIDIOC_S_TUNER rangelow/rangehigh OK";
                ok = setFrequencyHz(fhz);
            } else {
                qWarning() << "RadioWindow: VIDIOC_S_TUNER band switch failed:" << strerror(errno);
            }
        }

        // ── 方式 1：直接写入目标频率 ───────────────────────────────────────────
        ok = setFrequencyHz(fhz);

        // ── 方式 2：V4L2_CID_BAND_STOP_FILTER（某些 Allwinner BSP 驱动将其复用为频段选择器）
        if (!ok) {
            struct v4l2_control ctrl;
            memset(&ctrl, 0, sizeof(ctrl));
            ctrl.id    = V4L2_CID_BAND_STOP_FILTER;   // V4L2_CID_BASE + 33
            ctrl.value = m_isFM ? 0 : 1;              // 0=FM, 1=AM
            if (::ioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
                qDebug() << "RadioWindow: band switch via V4L2_CID_BAND_STOP_FILTER";
                ok = setFrequencyHz(fhz);
            } else {
                // 也试 value=1(FM)/0(AM)，反过来
                ctrl.value = m_isFM ? 1 : 0;
                if (::ioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
                    qDebug() << "RadioWindow: band switch via V4L2_CID_BAND_STOP_FILTER (inv)";
                    ok = setFrequencyHz(fhz);
                }
            }
        }

        // ── 方式 3：先 G_FREQUENCY 保留原结构体，仅改 frequency 字段再 S_FREQUENCY ─
        //          与 Qt 官方 v4lradiocontrol 实现一致；某些驱动用 reserved[] 保存频段
        if (!ok) {
            struct v4l2_frequency vfg;
            memset(&vfg, 0, sizeof(vfg));
            vfg.tuner = 0;
            vfg.type  = V4L2_TUNER_RADIO;
            if (::ioctl(m_fd, VIDIOC_G_FREQUENCY, &vfg) == 0) {
                vfg.frequency = driverFreq;
                if (::ioctl(m_fd, VIDIOC_S_FREQUENCY, &vfg) == 0) {
                    qDebug() << "RadioWindow: band switch via G_FREQ→modify→S_FREQ";
                    ok = true;
                }
            }
        }

        // ── 方式 4：VIDIOC_S_FREQUENCY 中 reserved[0] 置为频段标志 ─────────────
        //          部分 BSP 驱动将 reserved[0]=0 解释为 FM，=1 解释为 AM
        if (!ok) {
            for (int bandVal = 0; bandVal <= 3 && !ok; ++bandVal) {
                struct v4l2_frequency vfr;
                memset(&vfr, 0, sizeof(vfr));
                vfr.tuner      = 0;
                vfr.type       = V4L2_TUNER_RADIO;
                vfr.frequency  = driverFreq;
                vfr.reserved[0] = static_cast<__u32>(bandVal);
                if (::ioctl(m_fd, VIDIOC_S_FREQUENCY, &vfr) == 0) {
                    qDebug() << "RadioWindow: band switch via S_FREQ reserved[0]=" << bandVal;
                    ok = true;
                }
            }
        }

        // ── 方式 5：私有控制 V4L2_CID_PRIVATE_BASE + 0..31 ──────────────────────
        if (!ok) {
            for (int ofs = 0; ofs <= 31 && !ok; ++ofs) {
                struct v4l2_control ctrl;
                memset(&ctrl, 0, sizeof(ctrl));
                ctrl.id    = static_cast<__u32>(V4L2_CID_PRIVATE_BASE + ofs);
                ctrl.value = m_isFM ? 0 : 1;
                if (::ioctl(m_fd, VIDIOC_S_CTRL, &ctrl) == 0) {
                    qDebug() << "RadioWindow: band switch via V4L2_CID_PRIVATE_BASE+" << ofs;
                    ok = setFrequencyHz(fhz);
                }
            }
        }

        // ── 方式 6：VIDIOC_S_TUNER audmode 设为非标准值 ─────────────────────────
        //          某些驱动用 audmode=0 代表 AM，audmode=1 代表 FM
        if (!ok) {
            static const int auds[] = {0, 4, 8, 16, 3, 5};
            for (int audi : auds) {
                struct v4l2_tuner t;
                memset(&t, 0, sizeof(t));
                t.index   = 0;
                t.type    = V4L2_TUNER_RADIO;
                t.audmode = static_cast<__u32>(audi);
                if (::ioctl(m_fd, VIDIOC_S_TUNER, &t) == 0) {
                    qDebug() << "RadioWindow: band switch via S_TUNER audmode=" << audi;
                    ok = setFrequencyHz(fhz);
                    if (ok) break;
                }
            }
        }

        // ── 方式 7：尝试不同 tuner 索引（部分驱动将 AM 作为 tuner[1] 暴露）──────
        if (!ok) {
            for (int ti = 1; ti <= 3 && !ok; ++ti) {
                struct v4l2_frequency vft;
                memset(&vft, 0, sizeof(vft));
                vft.tuner     = static_cast<__u32>(ti);
                vft.type      = V4L2_TUNER_RADIO;
                vft.frequency = driverFreq;
                if (::ioctl(m_fd, VIDIOC_S_FREQUENCY, &vft) == 0) {
                    qDebug() << "RadioWindow: band switch via tuner index" << ti;
                    m_tunerIndex = ti;  // 记录 AM 使用的 tuner 索引
                    ok = true;
                }
            }
        }

        // ── 方式 8：音频输入选择（VIDIOC_S_AUDIO，某些驱动用输入区分 FM/AM）────────
        if (!ok) {
            const int targetAudio = m_isFM ? 0 : 1;
            struct v4l2_audio audio;
            memset(&audio, 0, sizeof(audio));
            audio.index = static_cast<__u32>(targetAudio);
            if (::ioctl(m_fd, VIDIOC_S_AUDIO, &audio) == 0) {
                qDebug() << "RadioWindow: band switch via VIDIOC_S_AUDIO index" << targetAudio;
                ok = setFrequencyHz(fhz);
                if (!ok) {
                    // 失败则恢复默认音频输入
                    audio.index = 0;
                    ::ioctl(m_fd, VIDIOC_S_AUDIO, &audio);
                }
            }
        }

        if (!ok) {
            qWarning() << "RadioWindow: band switch to" << (m_isFM ? "FM" : "AM")
                       << "failed — all methods exhausted";
        }
    }

    // 读回实际频率（驱动可能钳位到合法范围）
    quint32 v = getFrequencyHz();
    if (v > 0) {
        double freq = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);
        const double minFreq = m_isFM ? 87.0 : 522.0;
        const double maxFreq = m_isFM ? 108.0 : 1710.0;
        if (freq >= minFreq && freq <= maxFreq)
            m_frequency = freq;
    }

    rebuildStationStrip();
    updateFrequencyView();
}

void RadioWindow::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "[KeyPress] RadioWindow key=" << event->key()
             << "nativeScanCode=" << event->nativeScanCode()
             << "nativeVirtualKey=" << event->nativeVirtualKey();
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        qDebug() << "[KeyPress] => VolumeUp";
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%+"}, this);
        break;
    case Qt::Key_VolumeDown:
        qDebug() << "[KeyPress] => VolumeDown";
        AppSignals::runAmixer({"sset", "LINEOUT volume", "5%-"}, this);
        break;
    case Qt::Key_HomePage:
        qDebug() << "[KeyPress] => Home -> returnToMain";
        emit requestReturnToMain();
        close();
        break;
    case Qt::Key_Back:
        qDebug() << "[KeyPress] => Back -> returnToMain";
        emit requestReturnToMain();
        close();
        break;
    case Qt::Key_Escape:
        qDebug() << "[KeyPress] => Escape -> returnToMain";
        emit requestReturnToMain();
        close();
        break;
    default:
        qDebug() << "[KeyPress] => unhandled, passing to QMainWindow";
        QMainWindow::keyPressEvent(event);
    }
}
