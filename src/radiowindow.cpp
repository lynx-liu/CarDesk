#include "radiowindow.h"
#include "devicedetect.h"
#include "topbarwidget.h"
#include "t507sdkbridge.h"
#include "appsignals.h"

#include <QApplication>
#include <QCloseEvent>
#include "pagebgwidget.h"
#include <QKeyEvent>
#include <QProcess>
#include <QDateTime>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
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
#include <QtMath>
#include <algorithm>

// ── V4L2 ─────────────────────────────────────────────────────────────────────
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

// V4L2 频率单位：1/16 kHz。
// FM: 87.5 MHz = 87500 kHz => 87500*16 = 1400000
// AM:  531  kHz =   531 kHz =>   531*16 =    8496
static inline quint32 mhzToV4l2(double mhz) { return static_cast<quint32>(mhz * 1000.0 * 16.0); }
static inline quint32 khzToV4l2(double khz) { return static_cast<quint32>(khz * 16.0); }
static inline double  v4l2ToMhz(quint32 v)  { return v / 16000.0; }
static inline double  v4l2ToKhz(quint32 v)  { return v / 16.0; }

static void showRadioNoticeDialog(QWidget *parent, const QString &title, const QString &message)
{
    QDialog dialog(parent);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setModal(true);
    dialog.setFixedSize(1280, 720);
    dialog.setStyleSheet("background:transparent;");

    QWidget *overlay = new QWidget(&dialog);
    overlay->setGeometry(0, 0, 1280, 720);
    overlay->setStyleSheet("background:rgba(0,0,0,0.55);");

    QWidget *panel = new QWidget(&dialog);
    panel->setFixedSize(740, 300);
    panel->move((1280 - panel->width()) / 2, (720 - panel->height()) / 2);
    panel->setStyleSheet(
        "QWidget{background:rgba(12,18,32,0.98);border:1px solid rgba(0,104,255,0.55);border-radius:36px;}"
    );

    QWidget *accent = new QWidget(panel);
    accent->setGeometry(0, 0, panel->width(), 6);
    accent->setStyleSheet(
        "QWidget{background:qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #009CFF, stop:1 #00F0FF);border-top-left-radius:36px;border-top-right-radius:36px;}"
    );

    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(34, 26, 34, 24);
    panelLayout->setSpacing(16);
    panelLayout->setStretch(0, 0);
    panelLayout->setStretch(1, 1);
    panelLayout->setStretch(2, 0);

    QWidget *header = new QWidget(panel);
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(14);

    QLabel *icon = new QLabel(header);
    icon->setFixedSize(56, 56);
    icon->setText("!");
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(
        "QLabel{color:#FFFFFF;background:#00A8FF;border-radius:28px;"
        "font-size:34px;font-weight:900;box-shadow:0 0 18px rgba(0,168,255,0.45);}"
    );
    headerLayout->addWidget(icon);

    QLabel *titleLabel = new QLabel(title, header);
    titleLabel->setStyleSheet("color:#FFFFFF;font-size:34px;font-weight:800;background:transparent;");
    titleLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    panelLayout->addWidget(header);

    QLabel *messageLabel = new QLabel(message, panel);
    messageLabel->setWordWrap(true);
    messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    messageLabel->setStyleSheet("color:#DDE8FF;font-size:30px;line-height:44px;background:transparent;");
    panelLayout->addWidget(messageLabel, 1);

    QPushButton *confirmBtn = new QPushButton(parent->tr("我知道了"), panel);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setFixedSize(240, 64);
    confirmBtn->setStyleSheet(
        "QPushButton{border:none;background:#0068FF;color:#FFFFFF;"
        "font-size:32px;font-weight:700;border-radius:32px;}"
        "QPushButton:hover{background:#1A8EFF;}"
        "QPushButton:pressed{background:#005EC0;}"
    );

    QWidget *buttonWrap = new QWidget(panel);
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWrap);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(0);
    buttonLayout->addStretch();
    buttonLayout->addWidget(confirmBtn);
    buttonLayout->addStretch();
    panelLayout->addWidget(buttonWrap);

    QObject::connect(confirmBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

static void findBarScaleBounds(const QPixmap &pixmap, int &scaleStart, int &scaleEnd)
{
    const QImage img = pixmap.toImage().convertToFormat(QImage::Format_Grayscale8);
    const int width = img.width();
    const int height = img.height();
    QVector<double> colEdge(width - 1);
    for (int x = 0; x < width - 1; ++x) {
        long long sum = 0;
        for (int y = 0; y < height; ++y) {
            const int cur = qGray(img.pixel(x, y));
            const int nxt = qGray(img.pixel(x + 1, y));
            sum += qAbs(cur - nxt);
        }
        colEdge[x] = double(sum) / height;
    }
    double mean = 0.0, sq = 0.0;
    for (double v : colEdge) {
        mean += v;
        sq += v * v;
    }
    mean /= colEdge.size();
    const double stddev = qSqrt(qMax(0.0, sq / colEdge.size() - mean * mean));
    const double threshold = mean + stddev * 1.2;
    scaleStart = 0;
    for (int x = 10; x < qMin(width / 4, width - 1); ++x) {
        if (colEdge[x] > threshold) {
            scaleStart = x;
            break;
        }
    }
    scaleEnd = width - 1;
    for (int x = width - 2; x > qMax(width * 3 / 4, 0); --x) {
        if (colEdge[x] > threshold) {
            scaleEnd = x;
            break;
        }
    }
}

struct BarScaleConfig {
    int layoutStart = 0;
    int layoutEnd = 0;
    int scrollMin = 0;
    int scrollMax = 0;
    double minFreq = 87.0;
    double maxFreq = 108.0;
    bool amTwoPxPerKhz = false;
};

static constexpr double kAmFreqOriginKhz = 511.0;
static constexpr int kAmPxPerKhz = 2;
static constexpr int kAmScalePxEnd = 1140;  // 刻度区宽 1140px（2px/kHz => 570 kHz）

static BarScaleConfig barScaleConfig(bool isFm, const QPixmap &pixmap)
{
    BarScaleConfig cfg;
    if (isFm) {
        cfg.layoutStart = 0;
        cfg.layoutEnd = pixmap.width() - 1;
        findBarScaleBounds(pixmap, cfg.layoutStart, cfg.layoutEnd);
        cfg.scrollMin = 0;
        cfg.scrollMax = qMax(1, cfg.layoutEnd - cfg.layoutStart);
        cfg.minFreq = 87.0;
        cfg.maxFreq = 108.0;
        return cfg;
    }

    // AM：图像 x=0 对应 511 kHz，2px/kHz；531 kHz 在 x=40，可滑到 1629 kHz（x=2236）
    cfg.layoutStart = 0;
    cfg.layoutEnd = kAmScalePxEnd;
    cfg.amTwoPxPerKhz = true;
    cfg.minFreq = 531.0;
    cfg.maxFreq = 1629.0;
    cfg.scrollMin = kAmPxPerKhz * (531 - 511);
    cfg.scrollMax = kAmPxPerKhz * (1629 - 511);
    return cfg;
}

static double frequencyFromScroll(const BarScaleConfig &cfg, int scrollValue)
{
    if (cfg.amTwoPxPerKhz) {
        return kAmFreqOriginKhz + scrollValue / double(kAmPxPerKhz);
    }
    const int span = qMax(1, cfg.scrollMax - cfg.scrollMin);
    const double ratio = double(scrollValue - cfg.scrollMin) / span;
    return cfg.minFreq + ratio * (cfg.maxFreq - cfg.minFreq);
}

static int scrollFromFrequency(const BarScaleConfig &cfg, double frequency)
{
    if (cfg.amTwoPxPerKhz) {
        const double clamped = qBound(cfg.minFreq, frequency, cfg.maxFreq);
        return qRound(kAmPxPerKhz * (clamped - kAmFreqOriginKhz));
    }
    const double clamped = qBound(cfg.minFreq, frequency, cfg.maxFreq);
    const double ratio = (clamped - cfg.minFreq) / (cfg.maxFreq - cfg.minFreq);
    return cfg.scrollMin + qRound(ratio * (cfg.scrollMax - cfg.scrollMin));
}

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
    , m_isFM(true)
    , m_fmFrequency(95.9)
    , m_amFrequency(937.0)
    , m_frequency(95.9)
    , m_tunerCapLow(true)
    , m_tunerIndex(0)
    , m_favorite(false)
    , m_scanMode(false)
    , m_preserveAudioOnHide(false)
    , m_seekUpward(true)
    , m_seekStartFreq(95.9)
    , m_seekStepCount(0)
    , m_scanTimer(new QTimer(this))
    , m_barDragging(false)
    , m_barDragStartX(0)
    , m_barDragStartScroll(0)
    , m_stationDragging(false)
    , m_stationDragActive(false)
    , m_stationDragStartX(0)
    , m_stationDragStartScroll(0) {

        m_scanTimer->setInterval(120);
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
    
    auto sortFavorites = [](QStringList &list) {
        std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
            bool ok1 = false;
            bool ok2 = false;
            double v1 = a.toDouble(&ok1);
            double v2 = b.toDouble(&ok2);
            if (ok1 && ok2)
                return v1 < v2;
            return a < b;
        });
    };
    auto normalizeFavorites = [&](QStringList &list) {
        QStringList normalized;
        normalized.reserve(list.size());
        for (const QString &item : qAsConst(list)) {
            if (!normalized.contains(item))
                normalized.append(item);
        }
        sortFavorites(normalized);
        while (normalized.size() > 30)
            normalized.removeLast();
        list = std::move(normalized);
    };
    {
        QSettings settings;
        m_fmFavorites = settings.value("radio/fmFavorites").toStringList();
        m_amFavorites = settings.value("radio/amFavorites").toStringList();
        normalizeFavorites(m_fmFavorites);
        normalizeFavorites(m_amFavorites);
    }

    loadRadioState();

    // 尝试打开硬件设备
    if (openDevice()) {
        applyTunerBandAndFrequency();
        rebuildStationStrip();
    }

    updateFrequencyView();

    // 进入收音机界面时自动切到收音机声道
    T507SdkBridge::setAudioSource(true);
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
    persistRadioState();
    stopScan();
    closeDevice();
    // 退出收音机：将 TM2313 功放输入切回媒体声道（IN2 = SoC DAC）
    T507SdkBridge::setAudioSource(false);
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

void RadioWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_fd < 0 && openDevice()) {
        applyTunerBandAndFrequency();
        updateFrequencyView();
    }
    // 进入收音机界面时切回收音机声道
    T507SdkBridge::setAudioSource(true);
    m_preserveAudioOnHide = false;
    m_audioPreserved = false;
}

void RadioWindow::hideEvent(QHideEvent *event)
{
    persistRadioState();
    QMainWindow::hideEvent(event);
    if (m_preserveAudioOnHide) {
        qDebug() << "RadioWindow::hideEvent preserving audio source";
        m_preserveAudioOnHide = false;
        m_audioPreserved = true;
        return;
    }
    m_audioPreserved = false;
    // 隐藏收音机时切回媒体声道，防止电话呼出时仍然保留收音机音频
    T507SdkBridge::setAudioSource(false);
}

bool RadioWindow::isAudioActive() const
{
    return isVisible() || m_audioPreserved;
}

void RadioWindow::forceStopAudio()
{
    if (m_preserveAudioOnHide) {
        m_preserveAudioOnHide = false;
    }
    if (m_audioPreserved) {
        m_audioPreserved = false;
    }
    T507SdkBridge::setAudioSource(false);
}

void RadioWindow::restoreBackgroundAudio()
{
    T507SdkBridge::setAudioSource(true);
    if (!isVisible()) {
        m_audioPreserved = true;
    }
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
                const QPixmap *pix = m_barLabel->pixmap();
                const BarScaleConfig cfg = pix
                    ? barScaleConfig(m_isFM, *pix)
                    : barScaleConfig(m_isFM, QPixmap());
                sb->setValue(qBound(cfg.scrollMin, m_barDragStartScroll + delta, cfg.scrollMax));
                // 实时更新频率显示（不向驱动写入，避免过多 ioctl）
                const double freq = frequencyFromScroll(cfg, sb->value());
                const double clamped = qBound(cfg.minFreq, freq, cfg.maxFreq);
                if (m_freqLabel)
                    m_freqLabel->setText(m_isFM ? QString::number(clamped, 'f', 1)
                                                : QString::number(clamped, 'f', 0));
                m_frequency = clamped;
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
                persistRadioState();
                return true;
            }
            break;
        default:
            break;
        }
    }

    if (m_stationList && obj == m_stationList->viewport()) {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (me->button() == Qt::LeftButton) {
                m_stationDragging = true;
                m_stationDragActive = false;
                m_stationDragStartX = me->x();
                m_stationDragStartScroll = m_stationList->horizontalScrollBar()->value();
                m_stationList->viewport()->setCursor(Qt::ClosedHandCursor);
                return false; // allow item click to be delivered unless a drag gesture starts
            }
            break;
        case QEvent::MouseMove:
            if (m_stationDragging) {
                const int delta = m_stationDragStartX - me->x();
                if (!m_stationDragActive && qAbs(delta) < 6) {
                    return false;
                }
                m_stationDragActive = true;
                QScrollBar *sb = m_stationList->horizontalScrollBar();
                sb->setValue(qBound(0, m_stationDragStartScroll + delta, sb->maximum()));
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_stationDragging && me->button() == Qt::LeftButton) {
                m_stationDragging = false;
                m_stationList->viewport()->setCursor(Qt::OpenHandCursor);
                if (m_stationDragActive) {
                    m_stationDragActive = false;
                    return true;
                }
                return false;
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

bool RadioWindow::setRadioMute(bool mute)
{
    if (m_fd < 0) return false;
    qDebug() << "RadioWindow: setting mute to" << mute;

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
    setRadioMute(true);           // 开始搜台时静音
    m_scanTimer->stop();      // 停止旧的搜台（如果有）
    m_seekUpward    = upward;
    m_seekStartFreq = m_frequency;
    m_seekStepCount = 0;
    m_scanTimer->start();
    return true;
}

void RadioWindow::stopScan()
{
    m_scanMode      = false;
    if (m_scanTimer)
        m_scanTimer->stop();

    if (m_scanBtn) {
        m_scanBtn->setChecked(false);
        m_scanBtn->setDown(false);
    }
    m_seekStepCount = 0;
    setRadioMute(false);
    updateFrequencyView();
}

void RadioWindow::setupUI() {
    QWidget *central = new PageBgWidget(this);
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
    connect(homeBtn, &QPushButton::clicked, this, [this]{
        qDebug() << "RadioWindow home button clicked => preserve radio audio and hide";
        m_preserveAudioOnHide = true;
        emit requestReturnToMain();
        hide();
    });

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
    m_barContent = new QWidget();
    m_barContent->setStyleSheet("background:transparent;");
    m_barLabel = new QLabel(m_barContent);
    m_barLabel->setStyleSheet("background:transparent;");
    m_barScrollArea->setWidget(m_barContent);
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
        "QPushButton:pressed{background-image:url(:/images/butt_radio_list_down.png);}");
    listBtn->setCursor(Qt::PointingHandCursor);
    listBtn->setFocusPolicy(Qt::NoFocus);
    listBtn->setAutoDefault(false);
    listBtn->setDefault(false);
    connect(listBtn, &QPushButton::clicked, this, &RadioWindow::onOpenListDialog);

    m_searchBtn = new QPushButton(btnRow);
    m_searchBtn->setGeometry(248, 22, 60, 60);
    m_searchBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_up.png);}"
        "QPushButton:pressed{background-image:url(:/images/butt_radio_search_down.png);}");
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setFocusPolicy(Qt::NoFocus);
    m_searchBtn->setAutoDefault(false);
    m_searchBtn->setDefault(false);
    connect(m_searchBtn, &QPushButton::clicked, this, &RadioWindow::onSearch);

    m_favoriteBtn = new QPushButton(btnRow);
    m_favoriteBtn->setGeometry(496, 22, 60, 60);
    m_favoriteBtn->setCursor(Qt::PointingHandCursor);
    m_favoriteBtn->setFocusPolicy(Qt::NoFocus);
    m_favoriteBtn->setCheckable(true);
    m_favoriteBtn->setAutoDefault(false);
    m_favoriteBtn->setDefault(false);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &RadioWindow::onToggleFavorite);

    m_scanBtn = new QPushButton(btnRow);
    m_scanBtn->setGeometry(744, 22, 60, 60);
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setFocusPolicy(Qt::NoFocus);
    m_scanBtn->setCheckable(true);
    m_scanBtn->setAutoDefault(false);
    m_scanBtn->setDefault(false);
    m_scanBtn->setChecked(false);
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
    m_stationList->viewport()->installEventFilter(this);
    m_stationList->viewport()->setCursor(Qt::OpenHandCursor);
    rebuildStationStrip();

    connect(m_stationList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        m_frequency = item->text().toDouble();
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
        updateFrequencyView();
        persistRadioState();
    });
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
            const BarScaleConfig cfg = barScaleConfig(m_isFM, barPixmap);
            const int leftPadding = markerX - cfg.layoutStart;
            int rightPadding = viewportWidth - markerX - (barWidth - 1 - cfg.layoutEnd);
            int totalWidth = leftPadding + barWidth + rightPadding;
            if (cfg.amTwoPxPerKhz) {
                const int needWidth = cfg.scrollMax + viewportWidth;
                if (totalWidth < needWidth)
                    totalWidth = needWidth;
            }
            const double clamped = qBound(cfg.minFreq, m_frequency, cfg.maxFreq);
            const int scrollPos = scrollFromFrequency(cfg, clamped);
            m_barLabel->setPixmap(barPixmap);
            m_barLabel->setFixedSize(barWidth, 106);
            m_barLabel->move(leftPadding, 0);
            if (m_barContent)
                m_barContent->setFixedSize(totalWidth, 106);
            m_barScrollArea->horizontalScrollBar()->setRange(cfg.scrollMin, cfg.scrollMax);
            m_barScrollArea->horizontalScrollBar()->setValue(qBound(cfg.scrollMin, scrollPos, cfg.scrollMax));
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
        m_favoriteBtn->setChecked(m_favorite);
        m_favoriteBtn->setStyleSheet(m_favorite
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_collection_down.png);}" 
              "QPushButton:checked{background-image:url(:/images/butt_music_collection_down.png);}" 
            : "QPushButton{border:none;background-image:url(:/images/butt_music_collection_up.png);}" 
              "QPushButton:pressed{background-image:url(:/images/butt_music_collection_down.png);}" 
        );
    }
    if (m_scanBtn) {
        m_scanBtn->setChecked(m_scanMode);
        m_scanBtn->setStyleSheet(m_scanMode
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_scan_down.png);}" 
              "QPushButton:pressed{background-image:url(:/images/butt_music_scan_down.png);}" 
              "QPushButton:checked{background-image:url(:/images/butt_music_scan_down.png);}" 
            : "QPushButton{border:none;background-image:url(:/images/butt_music_scan_up.png);}" 
              "QPushButton:pressed{background-image:url(:/images/butt_music_scan_down.png);}" 
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
    const double minFreq = m_isFM ? 87.0 : 531.0;
    const double maxFreq = m_isFM ? 108.0 : 1629.0;
    m_frequency = qBound(minFreq, m_frequency - step, maxFreq);
    updateFrequencyView();
    persistRadioState();
}

void RadioWindow::onNext() {
    if (m_fd >= 0) {
        startAutoSeek(true);
        return;
    }
    // 无硬件：手动步进（FM 0.1MHz / AM 9kHz）
    const double step = m_isFM ? 0.1 : 9.0;
    const double minFreq = m_isFM ? 87.0 : 531.0;
    const double maxFreq = m_isFM ? 108.0 : 1629.0;
    m_frequency = qBound(minFreq, m_frequency + step, maxFreq);
    updateFrequencyView();
    persistRadioState();
}

void RadioWindow::onToggleFavorite() {
    const QString key = m_isFM ? QString::number(m_frequency, 'f', 1)
                                : QString::number(qRound(m_frequency));
    QStringList &favs = m_isFM ? m_fmFavorites : m_amFavorites;
    if (favs.contains(key)) {
        favs.removeAll(key);
    } else {
        const int maxFavorites = 30;
        if (favs.size() >= maxFavorites) {
            showRadioNoticeDialog(this, tr("收藏已满"),
                                  tr("当前收藏已达到 %1 个，请先删除旧收藏后再添加新频率。").arg(maxFavorites));
            return;
        }
        favs.append(key);
        std::sort(favs.begin(), favs.end(), [](const QString &a, const QString &b) {
            bool ok1 = false;
            bool ok2 = false;
            double v1 = a.toDouble(&ok1);
            double v2 = b.toDouble(&ok2);
            if (ok1 && ok2)
                return v1 < v2;
            return a < b;
        });
    }
    // 保存收藏到本地设置
    {
        QSettings settings;
        settings.setValue("radio/fmFavorites", m_fmFavorites);
        settings.setValue("radio/amFavorites", m_amFavorites);
    }
    rebuildStationStrip();
    updateFrequencyView();
}

void RadioWindow::onToggleScan() {
    m_scanMode = !m_scanMode;
    if (m_scanMode) {
        // 开始连续自动扫台（用户空间逐频点）
        if (m_fd >= 0) startAutoSeek(true);
    } else {
        // 停止扫台
        stopScan();
    }
    updateFrequencyView();
    if (!m_scanMode)
        persistRadioState();
}

void RadioWindow::onScanTick() {
    const double step    = m_isFM ? 0.1   : 9.0;
    const double minFreq = m_isFM ? 87.0  : 531.0;
    const double maxFreq = m_isFM ? 108.0 : 1629.0;
    // 动态计算步数：频带宽度除以步长，再留出少量余量
    const int maxSteps = int((maxFreq - minFreq) / step) + 2;

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
            // signal 为 __s32，用有符号比较
            const int threshold = m_isFM ? 22500 : 35000;
            if (tuner.signal > threshold) {
                // 找到电台！
                m_scanTimer->stop();
                setRadioMute(false);  // 扫到台了先取消静音
                updateFrequencyView();
                persistRadioState();
                if (m_scanMode) {
                    // 连续扫台：停留1.5s后继续
                    QTimer::singleShot(1500, this, [this]() {
                        if (m_scanMode) {
                            m_seekStepCount = 0;
                            m_seekStartFreq = m_frequency;
                            m_scanTimer->start();
                            setRadioMute(true); // 继续搜台时再次静音
                        }
                    });
                }
                return;
            }
        }
    }

    // ② 检测是否已绕一圈
    if (m_seekStepCount >= maxSteps) {
        stopScan();
        updateFrequencyView();
        persistRadioState();
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
    updateFrequencyView();
}

void RadioWindow::onSearch() {
    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(1280, 720);

    auto *dlgBg = new PageBgWidget(&dialog);
    dlgBg->setGeometry(0, 0, 1280, 720);
    dlgBg->lower();

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
    //   AM: 纯整数，最多4位(531~1629)，不允许小数点
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
        m_frequency = m_isFM ? qBound(87.0, v, 108.0) : qBound(531.0, v, 1629.0);
        quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
        setFrequencyHz(fhz);
        updateFrequencyView();
        persistRadioState();
    }
}

void RadioWindow::onOpenListDialog() {
    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(1280, 720);

    auto *dlgBg = new PageBgWidget(&dialog);
    dlgBg->setGeometry(0, 0, 1280, 720);
    dlgBg->lower();

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

    // 电台网格：仅显示用户收藏
    // CSS .radio_list_con { width:1060; margin:16px auto }
    // 宽度改为 1092，以便出现垂直滚动条时仍能保持5列布局
    QListWidget *list = new QListWidget(&dialog);
    list->setGeometry(107, 182, 1092, 424);
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
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setMouseTracking(true);
    list->viewport()->setMouseTracking(true);
    list->setAttribute(Qt::WA_Hover);
    list->setStyleSheet(
        "QListWidget{border:none;background:transparent;outline:none;padding:0;margin:0;}"
        "QListWidget::item{width:212px;height:212px;background:transparent;}"
        "QScrollBar:vertical{width:12px;background:transparent;border-radius:6px;margin:0;padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px;padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;background:none;border:none;}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}");
    list->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical{width:12px;background:transparent;border-radius:6px;margin:0;padding:0;}"
        "QScrollBar::groove:vertical{background:rgba(0,104,255,0.10);border-radius:3px;margin:0px 3px;padding:0;}"
        "QScrollBar::handle:vertical{background:#0068FF;border-radius:3px;min-height:60px;margin:3px 3px;}"
        "QScrollBar::handle:vertical:hover{background:#00faff;}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical{height:0;background:none;border:none;}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical{background:transparent;}");
    list->setItemDelegate(new RadioListDelegate(m_isFM, list));

    QLabel *hint = nullptr;
    auto refillList = [&]() {
        if (hint) { delete hint; hint = nullptr; }
        list->clear();
        const QStringList &src = m_isFM ? m_fmFavorites : m_amFavorites;
        const int maxItems = 30;
        const QStringList srcLimited = src.mid(0, qMin(src.size(), maxItems));
        for (const QString &s : srcLimited) {
            QListWidgetItem *it = new QListWidgetItem(list);
            it->setData(Qt::UserRole, s);
            it->setSizeHint(QSize(212, 212));
        }
        if (src.isEmpty()) {
            hint = new QLabel(m_isFM ? "暂无收藏的 FM 电台\n\n在播放界面点击 ♡ 收藏当前频率"
                                     : "暂无收藏的 AM 电台\n\n在播放界面点击 ♡ 收藏当前频率",
                              list->viewport());
            hint->setStyleSheet("color:#aaa;font-size:28px;background:transparent;");
            hint->setAlignment(Qt::AlignCenter);
            hint->setGeometry(0, 50, 1066, 200);
            hint->show();
        }
    };

    refillList();

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
        persistRadioState();
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
    const QStringList stations = m_isFM ? m_fmFavorites : m_amFavorites;
    const int maxItems = 30;
    const int count = qMin(stations.size(), maxItems);
    for (int i = 0; i < count; ++i) {
        QListWidgetItem *it = new QListWidgetItem(stations.at(i), m_stationList);
        it->setSizeHint(QSize(150, 118));
        m_stationList->addItem(it);
    }
}

bool RadioWindow::applyTunerBandAndFrequency()
{
    if (m_fd < 0)
        return false;

    // tea685x：VIDIOC_S_TUNER 用 rangelow/rangehigh 选 AM/FM；开机默认多为 FM，
    // 若只写 AM 频率（如 12240≈765kHz）会 EINVAL。
    m_tunerIndex = 0;
    const quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
    struct v4l2_tuner t;
    memset(&t, 0, sizeof(t));
    t.index     = 0;
    t.type      = V4L2_TUNER_RADIO;
    t.audmode   = V4L2_TUNER_MODE_STEREO;
    t.rangelow  = m_isFM ? 1392000u : 8352u;
    t.rangehigh = m_isFM ? 1728000u : 27360u;
    if (::ioctl(m_fd, VIDIOC_S_TUNER, &t) != 0) {
        qWarning() << "RadioWindow: VIDIOC_S_TUNER band switch failed:" << strerror(errno)
                   << "(driver must support S_TUNER with rangelow/rangehigh)";
        return false;
    }
    qDebug() << "RadioWindow: band switch via VIDIOC_S_TUNER rangelow/rangehigh OK"
             << (m_isFM ? "FM" : "AM") << "freq=" << m_frequency;
    if (!setFrequencyHz(fhz)) {
        qWarning() << "RadioWindow: setFrequencyHz failed after S_TUNER:" << strerror(errno);
        return false;
    }

    const quint32 v = getFrequencyHz();
    if (v > 0) {
        const double freq = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);
        const double minFreq = m_isFM ? 87.0 : 531.0;
        const double maxFreq = m_isFM ? 108.0 : 1629.0;
        if (freq >= minFreq && freq <= maxFreq) {
            m_frequency = freq;
            syncCurrentBandFrequency();
        }
    }
    return true;
}

void RadioWindow::switchBand(bool fm) {
    stopScan();
    syncCurrentBandFrequency();
    m_isFM = fm;
    m_frequency = m_isFM ? m_fmFrequency : m_amFrequency;

    if (m_fd >= 0)
        applyTunerBandAndFrequency();

    syncCurrentBandFrequency();
    persistRadioState();
    rebuildStationStrip();
    updateFrequencyView();
}

void RadioWindow::loadRadioState()
{
    QSettings settings;
    m_isFM = settings.value(QStringLiteral("radio/isFm"), true).toBool();
    m_fmFrequency = settings.value(QStringLiteral("radio/fmFrequency"), 95.9).toDouble();
    m_amFrequency = settings.value(QStringLiteral("radio/amFrequency"), 937.0).toDouble();
    m_fmFrequency = qBound(87.0, m_fmFrequency, 108.0);
    m_amFrequency = qBound(531.0, m_amFrequency, 1629.0);
    m_frequency = m_isFM ? m_fmFrequency : m_amFrequency;
}

void RadioWindow::syncCurrentBandFrequency()
{
    if (m_isFM)
        m_fmFrequency = m_frequency;
    else
        m_amFrequency = m_frequency;
}

void RadioWindow::persistRadioState()
{
    syncCurrentBandFrequency();
    QSettings settings;
    settings.setValue(QStringLiteral("radio/isFm"), m_isFM);
    settings.setValue(QStringLiteral("radio/fmFrequency"), m_fmFrequency);
    settings.setValue(QStringLiteral("radio/amFrequency"), m_amFrequency);
}

void RadioWindow::keyPressEvent(QKeyEvent *event)
{
    qDebug() << "[KeyPress] RadioWindow key=" << event->key()
             << "nativeScanCode=" << event->nativeScanCode()
             << "nativeVirtualKey=" << event->nativeVirtualKey();
    switch (event->key()) {
    case Qt::Key_VolumeUp:
        qDebug() << "[KeyPress] => VolumeUp";
        AppSignals::changeVolume(+1);
        break;
    case Qt::Key_VolumeDown:
        qDebug() << "[KeyPress] => VolumeDown";
        AppSignals::changeVolume(-1);
        break;
    case Qt::Key_MediaPrevious:
        qDebug() << "[KeyPress] => MediaPrevious";
        onPrev();
        break;
    case Qt::Key_MediaNext:
        qDebug() << "[KeyPress] => MediaNext";
        onNext();
        break;
    case Qt::Key_HomePage:
        qDebug() << "[KeyPress] => Home -> returnToMain without stopping radio";
        m_preserveAudioOnHide = true;
        emit requestReturnToMain();
        hide();
        break;
    case Qt::Key_Back:
        qDebug() << "[KeyPress] => Back -> returnToMain and stop radio";
        emit requestReturnToMain();
        close();
        break;
    case Qt::Key_Escape:
        qDebug() << "[KeyPress] => Escape -> returnToMain and stop radio";
        emit requestReturnToMain();
        close();
        break;
    default:
        qDebug() << "[KeyPress] => unhandled, passing to QMainWindow";
        QMainWindow::keyPressEvent(event);
    }
}
