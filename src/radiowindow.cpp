#include "radiowindow.h"
#include "devicedetect.h"

#include <QApplication>
#include <QCloseEvent>
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
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QDebug>

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
    , m_playBtn(nullptr)
    , m_favoriteBtn(nullptr)
    , m_scanBtn(nullptr)
    , m_stationList(nullptr)
    , m_fmStations({"88.7", "90.6", "91.2", "92.5", "95.9", "96.3", "97.7", "99.8", "101.1"})
    , m_amStations({"554", "639", "756", "855", "937", "955", "981", "1008", "1143"})
    , m_isFM(true)
    , m_frequency(95.9)
    , m_favorite(false)
    , m_scanMode(false)
    , m_playing(false)
    , m_scanTimer(new QTimer(this)) {

    m_scanTimer->setInterval(300);
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

    // 尝试打开硬件设备
    if (openDevice()) {
        // 静音先关掉，等用户点播放再开
        setMute(true);
        // 读回硬件当前频率作为初始值
        quint32 v = getFrequencyHz();
        if (v > 0)
            m_frequency = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);
    }

    updateFrequencyView();
}

RadioWindow::~RadioWindow()
{
    stopScan();
    if (m_playing) setMute(true);
    closeDevice();
}

void RadioWindow::closeEvent(QCloseEvent *event) {
    stopScan();
    if (m_playing) setMute(true);
    closeDevice();
    emit requestReturnToMain();
    QMainWindow::closeEvent(event);
}

// ══════════════════════════════════════════════════════════════════════════════
// V4L2 底层接口
// ══════════════════════════════════════════════════════════════════════════════

bool RadioWindow::openDevice()
{
    if (m_fd >= 0) return true;
    m_fd = ::open("/dev/radio0", O_RDWR | O_NONBLOCK);
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
    struct v4l2_frequency vf;
    vf.tuner = 0;
    vf.type  = V4L2_TUNER_RADIO;
    vf.frequency = freqHz;
    if (::ioctl(m_fd, VIDIOC_S_FREQUENCY, &vf) < 0) {
        qWarning() << "RadioWindow: VIDIOC_S_FREQUENCY failed:" << strerror(errno);
        return false;
    }
    return true;
}

quint32 RadioWindow::getFrequencyHz() const
{
    if (m_fd < 0) return 0;
    struct v4l2_frequency vf;
    vf.tuner = 0;
    vf.type  = V4L2_TUNER_RADIO;
    if (::ioctl(m_fd, VIDIOC_G_FREQUENCY, &vf) < 0) {
        qWarning() << "RadioWindow: VIDIOC_G_FREQUENCY failed:" << strerror(errno);
        return 0;
    }
    return vf.frequency;
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

bool RadioWindow::startAutoSeek(bool upward)
{
    if (m_fd < 0) return false;
    struct v4l2_hw_freq_seek seek;
    memset(&seek, 0, sizeof(seek));
    seek.tuner    = 0;
    seek.type     = V4L2_TUNER_RADIO;
    seek.seek_upward   = upward ? 1 : 0;
    seek.wrap_around   = 1;
    seek.spacing  = 0;   // 驱动自行决定步进
    if (::ioctl(m_fd, VIDIOC_S_HW_FREQ_SEEK, &seek) < 0) {
        qWarning() << "RadioWindow: VIDIOC_S_HW_FREQ_SEEK failed:" << strerror(errno);
        return false;
    }
    return true;
}

void RadioWindow::stopScan()
{
    if (m_scanTimer) m_scanTimer->stop();
    m_scanMode = false;
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
    homeBtn->setGeometry(12, 12, 48, 48);
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
    connect(m_fmTabBtn, &QPushButton::clicked, this, &RadioWindow::onSwitchFM);

    m_amTabBtn = new QPushButton("AM", central);
    m_amTabBtn->setGeometry(640, 100, 160, 66);
    m_amTabBtn->setCursor(Qt::PointingHandCursor);
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
        m_freqLabel->setStyleSheet("color:#fff;font-size:120px;background:transparent;");
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
    QLabel *stereo = new QLabel("STEREO", central);
    stereo->setGeometry(240, 186, 220, 120);
    stereo->setStyleSheet("color:#00FAFF;font-size:36px;background:transparent;");
    stereo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    stereo->setAttribute(Qt::WA_TransparentForMouseEvents);

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
    connect(prev, &QPushButton::clicked, this, &RadioWindow::onPrev);

    // barArea: CSS .radio_pro div { width:720; height:106; overflow-x:auto; overflow-y:hidden }
    //           ::-webkit-scrollbar { display:none } → QScrollArea with AlwaysOff
    m_barScrollArea = new QScrollArea(central);
    m_barScrollArea->setGeometry(232, 356, 720, 106);
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

    QPushButton *nextBtn = new QPushButton(central);
    nextBtn->setGeometry(952, 326, 120, 120);
    nextBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_searchnext_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_searchnext_down.png);}");
    nextBtn->setCursor(Qt::PointingHandCursor);
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

    // 标尺针：居中于 radio_pro， x=112+528-4=636, y=334
    QLabel *mark = new QLabel(central);
    mark->setGeometry(636, 334, 8, 85);
    mark->setPixmap(QPixmap(":/images/pict_radio_mark.png"));
    mark->setStyleSheet("background:transparent;");
    mark->setAttribute(Qt::WA_TransparentForMouseEvents);
    mark->raise();

    // ── 控制按钟区 (238,482,804×94) ───────────────────────────────────────
    // CSS .radio_btn { width:804; margin:20px auto } radio_pro bottom=462, +20=482
    // CSS justify-content:space-between 5个按鈕：总宽=60+60+84+60+60=324, 间距=(804-324)/4=120
    // x: list=0 search=180 play=360 fav=564 scan=744
    // y: 60px 按鈕 margin-top:12(+ul margin-top:10)=22; 84px play margin-top:0(+10)=10
    QWidget *btnRow = new QWidget(central);
    btnRow->setGeometry(238, 482, 804, 94);
    btnRow->setStyleSheet("background:transparent;");

    QPushButton *listBtn = new QPushButton(btnRow);
    listBtn->setGeometry(0, 22, 60, 60);
    listBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_list_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_list_down.png);}");
    listBtn->setCursor(Qt::PointingHandCursor);
    connect(listBtn, &QPushButton::clicked, this, &RadioWindow::onOpenListDialog);

    m_searchBtn = new QPushButton(btnRow);
    m_searchBtn->setGeometry(180, 22, 60, 60);
    m_searchBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_radio_search_up.png);}"
        "QPushButton:hover{background-image:url(:/images/butt_radio_search_down.png);}");
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &RadioWindow::onSearch);

    m_playBtn = new QPushButton(btnRow);
    m_playBtn->setGeometry(360, 10, 84, 84);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setStyleSheet(
        "QPushButton{border:none;background-image:url(:/images/butt_music_play_up.png);}"
        "QPushButton:pressed{background-image:url(:/images/butt_music_play_down.png);}");
    connect(m_playBtn, &QPushButton::clicked, this, &RadioWindow::onTogglePlay);

    m_favoriteBtn = new QPushButton(btnRow);
    m_favoriteBtn->setGeometry(564, 22, 60, 60);
    m_favoriteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &RadioWindow::onToggleFavorite);

    m_scanBtn = new QPushButton(btnRow);
    m_scanBtn->setGeometry(744, 22, 60, 60);
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &RadioWindow::onToggleScan);

    // ── 电台列表 (81,592,1118×118) ─────────────────────────────────────────
    // CSS .radio_play_list { width:1118; margin:16px auto }
    //   => x=(1280-1118)/2=81, y=btn_bottom(576)+16=592, h=118
    m_stationList = new QListWidget(central);
    m_stationList->setGeometry(81, 592, 1118, 118);
    m_stationList->setFlow(QListView::LeftToRight);
    m_stationList->setWrapping(false);
    m_stationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stationList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_stationList->setIconSize(QSize(1, 1));
    m_stationList->setGridSize(QSize(150, 118));
    m_stationList->setSpacing(0);
    m_stationList->setStyleSheet(
        "QListWidget{background:transparent;border:none;outline:none;}"
        "QListWidget::item{"
        "  width:150px;height:118px;"
        "  background-image:url(:/images/radio_play_list_up.png);"
        "  background-repeat:no-repeat;background-position:center;"
        "  font-size:36px;color:#fff;text-align:center;"
        "}"
        "QListWidget::item:selected{"
        "  background-image:url(:/images/radio_play_list_down.png);color:#00faff;"
        "}"
        "QListWidget::item:hover{"
        "  background-image:url(:/images/radio_play_list_down.png);color:#00faff;"
        "}"
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
        m_unitLabel->setText("MHz");
    }
    if (m_barScrollArea && m_barLabel) {
        const QString barPath = m_isFM ? QStringLiteral(":/images/pict_radio_fmbar.png")
                                       : QStringLiteral(":/images/pict_radio_ambar.png");
        const QPixmap barPixmap(barPath);
        if (!barPixmap.isNull()) {
            const int viewportWidth = 720;
            const int markerX = 408;
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
    // 有硬件：触发向下 seek；无硬件：在预设列表里循环
    if (m_fd >= 0) {
        startAutoSeek(false);
        // seek 是异步的，在 tick 里读回结果；此处给个 300ms 延时轮询
        m_scanTimer->start();
        QTimer::singleShot(500, this, [this]() { m_scanTimer->stop(); updateFrequencyView(); });
        return;
    }
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    const QString now = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
    int idx = stations.indexOf(now);
    if (idx < 0) idx = 0;
    idx = (idx + stations.size() - 1) % stations.size();
    m_frequency = stations[idx].toDouble();
    updateFrequencyView();
}

void RadioWindow::onNext() {
    if (m_fd >= 0) {
        startAutoSeek(true);
        m_scanTimer->start();
        QTimer::singleShot(500, this, [this]() { m_scanTimer->stop(); updateFrequencyView(); });
        return;
    }
    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    const QString now = m_isFM ? QString::number(m_frequency, 'f', 1) : QString::number(m_frequency, 'f', 0);
    int idx = stations.indexOf(now);
    if (idx < 0) idx = 0;
    idx = (idx + 1) % stations.size();
    m_frequency = stations[idx].toDouble();
    updateFrequencyView();
}

void RadioWindow::onToggleFavorite() {
    m_favorite = !m_favorite;
    updateFrequencyView();
}

void RadioWindow::onToggleScan() {
    m_scanMode = !m_scanMode;
    if (m_scanMode) {
        // 开始自动向上扫台，每 300ms 读一次当前频率
        if (m_fd >= 0) startAutoSeek(true);
        m_scanTimer->start();
    } else {
        m_scanTimer->stop();
    }
    updateFrequencyView();
}

void RadioWindow::onScanTick() {
    // 读回驱动当前停靠频率，刷新显示
    quint32 v = getFrequencyHz();
    if (v > 0) {
        double newFreq = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);
        if (qAbs(newFreq - m_frequency) > 0.05) {
            m_frequency = newFreq;
            updateFrequencyView();
        }
    }
}

void RadioWindow::onTogglePlay() {
    m_playing = !m_playing;
    setMute(!m_playing);
    if (m_playBtn) {
        m_playBtn->setStyleSheet(m_playing
            ? "QPushButton{border:none;background-image:url(:/images/butt_music_stop_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_play_up.png);}"
            : "QPushButton{border:none;background-image:url(:/images/butt_music_play_up.png);} QPushButton:hover{background-image:url(:/images/butt_music_stop_up.png);}"
        );
    }
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
    QLabel *titleLbl = new QLabel("收音机", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);

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
        m_frequency = m_isFM ? qBound(87.5, v, 108.0) : qBound(531.0, v, 1602.0);
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
    QLabel *titleLbl = new QLabel("收音机", topBar);
    titleLbl->setGeometry(0, 10, 1280, 54);
    titleLbl->setStyleSheet("color:#fff;font-size:36px;font-weight:bold;background:transparent;");
    titleLbl->setAlignment(Qt::AlignCenter);

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

    connect(favTab, &QPushButton::clicked, &dialog, [=]() {
        favTab->setStyleSheet(styleTabOn(true));
        localTab->setStyleSheet(styleTabOff(false));
    });
    connect(localTab, &QPushButton::clicked, &dialog, [=]() {
        localTab->setStyleSheet(styleTabOn(false));
        favTab->setStyleSheet(styleTabOff(true));
    });

    // 电台网格： CSS .radio_list_con { width:1060; margin:16px auto }
    //   => x=(1280-1060)/2=110, y=tab_bottom(166)+16=182, 1060×424
    QListWidget *list = new QListWidget(&dialog);
    list->setGeometry(110, 182, 1060, 424);
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
        "QListWidget{border:none;background:transparent;outline:none;}"
        "QListWidget::item{width:212px;height:212px;background:transparent;}"
        "QScrollBar:vertical,QScrollBar:horizontal{width:0;height:0;background:transparent;}");
    list->setItemDelegate(new RadioListDelegate(m_isFM, list));

    const QStringList stations = m_isFM ? m_fmStations : m_amStations;
    for (const QString &s : stations) {
        QListWidgetItem *it = new QListWidgetItem(list);
        it->setData(Qt::UserRole, s);
        it->setSizeHint(QSize(212, 212));
    }

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
    QWidget *right = new QWidget(topBar);
    right->setGeometry(1280 - 16 - 280, 12, 280, 48);
    right->setStyleSheet("background:transparent;");
    QHBoxLayout *rightLay = new QHBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(16);

    QLabel *btIcon = new QLabel(right);
    btIcon->setFixedSize(48, 48);
    btIcon->setPixmap(QPixmap(":/images/pict_bluetooth.png"));
    rightLay->addWidget(btIcon);

    QLabel *usbIcon = new QLabel(right);
    usbIcon->setFixedSize(48, 48);
    usbIcon->setPixmap(QPixmap(":/images/pict_usb.png"));
    rightLay->addWidget(usbIcon);

    QLabel *volIcon = new QLabel(right);
    volIcon->setFixedSize(48, 48);
    volIcon->setPixmap(QPixmap(":/images/pict_volume.png"));
    QLabel *volLabel = new QLabel("10", right);
    volLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(volIcon);
    rightLay->addWidget(volLabel);

    QLabel *timeLabel = new QLabel(QDateTime::currentDateTime().toString("hh:mm"), right);
    timeLabel->setStyleSheet("QLabel{color:#fff;font-size:36px;}");
    rightLay->addWidget(timeLabel);
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

    // 切换硬件 band（通过设置对应频率来隐式切换调谐器范围）
    quint32 fhz = m_isFM ? mhzToV4l2(m_frequency) : khzToV4l2(m_frequency);
    setFrequencyHz(fhz);

    // 读回实际频率（驱动可能钳位到合法范围）
    quint32 v = getFrequencyHz();
    if (v > 0)
        m_frequency = m_isFM ? v4l2ToMhz(v) : v4l2ToKhz(v);

    rebuildStationStrip();
    updateFrequencyView();
}
