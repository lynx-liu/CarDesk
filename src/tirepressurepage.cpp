#include "tirepressurepage.h"

#include <QColor>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPixmap>
#include <QShowEvent>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

namespace {

constexpr int kStaleMs = 3000;
constexpr int kCellH = 60;
/** CSS .system_tire li span:first-child / :last-child */
constexpr int kPressureFontPx = 32;
constexpr int kTemperatureFontPx = 24;
/** CSS .system_tire li { margin-bottom } */
constexpr int kFirstAfterGap = 37;
constexpr int kNormalAfterGap = 12;
constexpr int kWrapW = 1000;
constexpr int kBlockW = 600;
constexpr int kBlockLeft = 200;

const char *kColorPressureIdle = "#888888";
const char *kColorTemperatureIdle = "#607E9D";
const char *kColorTemperatureLive = "#00FAFF";
const char *kColorTemperatureStale = "#666666";

quint32 slotKey(int axle, int tire)
{
    return (static_cast<quint32>(axle & 0xff) << 8) | static_cast<quint32>(tire & 0xff);
}

} // namespace

void TirePressurePage::stylePressureLabel(QLabel *label, const QColor &color)
{
    if (!label) {
        return;
    }
    QFont font = label->font();
    font.setPixelSize(kPressureFontPx);
    font.setBold(false);
    label->setFont(font);
    // 不能用 font-size 当高度，否则字形下半截被裁切
    label->setFixedHeight(QFontMetrics(font).height());
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setProperty("tireRole", QStringLiteral("pressure"));
    label->setStyleSheet(
        QStringLiteral("QLabel{color:%1;font-size:%2px;"
                       "background:transparent;border:none;padding:0;margin:0;}")
            .arg(color.name(), QString::number(kPressureFontPx)));
}

void TirePressurePage::styleTemperatureLabel(QLabel *label, const QColor &color)
{
    if (!label) {
        return;
    }
    QFont font = label->font();
    font.setPixelSize(kTemperatureFontPx);
    font.setBold(false);
    label->setFont(font);
    label->setFixedHeight(QFontMetrics(font).height());
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setProperty("tireRole", QStringLiteral("temperature"));
    label->setStyleSheet(
        QStringLiteral("QLabel{color:%1;font-size:%2px;"
                       "background:transparent;border:none;padding:0;margin:0;}")
            .arg(color.name(), QString::number(kTemperatureFontPx)));
}

TirePressurePage::TirePressurePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tirePressurePage"));
    setStyleSheet(
        QStringLiteral("#tirePressurePage{background:transparent;border:none;}"
                       "#tirePressurePage QLabel[tireRole=\"pressure\"]{font-size:32px;}"
                       "#tirePressurePage QLabel[tireRole=\"temperature\"]{font-size:24px;}"));
    setFixedWidth(kWrapW);
    setupUi();

    m_staleTimer = new QTimer(this);
    m_staleTimer->setInterval(1000);
    connect(m_staleTimer, &QTimer::timeout, this, &TirePressurePage::refreshStale);

    if (McuSerialReader *mcu = McuSerialReader::ensureShared()) {
        connect(mcu, &McuSerialReader::tpmsReceived, this, &TirePressurePage::onTpmsReceived);
    }
}

void TirePressurePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_staleTimer) {
        m_staleTimer->start();
    }
    refreshStale();
}

void TirePressurePage::hideEvent(QHideEvent *event)
{
    if (m_staleTimer) {
        m_staleTimer->stop();
    }
    QWidget::hideEvent(event);
}

QWidget *TirePressurePage::makeColumn(QWidget *parent, int marginLeft, int marginRight)
{
    auto *col = new QWidget(parent);
    col->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *lay = new QVBoxLayout(col);
    // CSS: ul { margin: 16px 12px 8px 12px }，左右可由 nth-child 覆盖
    lay->setContentsMargins(marginLeft, 16, marginRight, 8);
    lay->setSpacing(0);
    return col;
}

void TirePressurePage::addUnitCell(QWidget *column, const QString &text, bool topPad, int afterGap)
{
    auto *lab = new QLabel(text, column);
    lab->setFixedHeight(kCellH);
    // HTML: Bar 为 first-child → 白；℃ 单 span 兼 last-child → #00FAFF
    const bool isTempUnit = text.contains(QStringLiteral("℃"));
    lab->setAlignment(isTempUnit ? (Qt::AlignRight | Qt::AlignTop)
                                 : (Qt::AlignHCenter | Qt::AlignTop));
    lab->setStyleSheet(
        QStringLiteral("QLabel{color:%1;font-size:32px;font-weight:bold;"
                       "background:transparent;border:none;padding-top:%2px;}")
            .arg(isTempUnit ? QStringLiteral("#00FAFF") : QStringLiteral("#FFFFFF"))
            .arg(topPad ? 8 : 0));
    auto *lay = static_cast<QVBoxLayout *>(column->layout());
    lay->addWidget(lab);
    if (afterGap > 0) {
        lay->addSpacing(afterGap);
    }
}

TirePressurePage::TireCell *TirePressurePage::addDataCell(QWidget *column, int axle, int tire, int afterGap)
{
    auto *wrap = new QWidget(column);
    wrap->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto *pressure = new QLabel(QStringLiteral("--"), wrap);
    stylePressureLabel(pressure, QColor(kColorPressureIdle));

    auto *temp = new QLabel(QStringLiteral("--"), wrap);
    styleTemperatureLabel(temp, QColor(kColorTemperatureIdle));

    lay->addWidget(pressure);
    lay->addWidget(temp);
    // CSS li height:60；Qt 需按字体 metrics 放宽，避免裁字
    wrap->setFixedHeight(qMax(kCellH, pressure->height() + 4 + temp->height()));

    auto *colLay = static_cast<QVBoxLayout *>(column->layout());
    colLay->addWidget(wrap);
    if (afterGap > 0) {
        colLay->addSpacing(afterGap);
    }

    auto *cell = new TireCell;
    cell->pressure = pressure;
    cell->temperature = temp;
    cell->axle = axle;
    cell->tire = tire;
    m_cells.append(cell);
    return cell;
}

void TirePressurePage::setupUi()
{
    // 对齐 HTML .system_infor.system_tire_wrap：宽 1000，相对定位放 L/R
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *wrap = new QWidget(this);
    wrap->setObjectName(QStringLiteral("tireWrap"));
    wrap->setFixedWidth(kWrapW);
    wrap->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(0, 0, 0, 0);
    wrapLay->setSpacing(0);

    auto buildBlock = [&](const QString &title, int titleTop, const QString &bgPath, int bgTop,
                          bool dashed, bool tractor) -> QWidget * {
        auto *block = new QWidget(wrap);
        block->setFixedWidth(kBlockW);
        auto *blockLay = new QVBoxLayout(block);
        blockLay->setContentsMargins(0, 0, 0, 0);
        blockLay->setSpacing(0);

        blockLay->addSpacing(titleTop);
        auto *titleLab = new QLabel(title, block);
        titleLab->setAlignment(Qt::AlignCenter);
        titleLab->setFixedHeight(36);
        titleLab->setStyleSheet(
            QStringLiteral("QLabel{color:#eaf2ff;font-size:36px;font-weight:bold;"
                           "background:transparent;border:none;}"));
        blockLay->addWidget(titleLab);

        auto *con = new QWidget(block);
        con->setStyleSheet(dashed
                               ? QStringLiteral("background:transparent;border:none;"
                                                "border-bottom:1px dashed rgba(255,255,255,191);")
                               : QStringLiteral("background:transparent;border:none;"));

        auto *bg = new QLabel(con);
        const QPixmap pm(bgPath);
        bg->setPixmap(pm);
        bg->setFixedSize(pm.size());
        bg->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
        bg->setAttribute(Qt::WA_TransparentForMouseEvents);
        bg->lower();

        auto *row = new QHBoxLayout(con);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        row->addStretch(1);

        // CSS: ul margin 12；nth-child(2) margin-right:70；nth-child(3) margin-left:70
        QWidget *c1 = makeColumn(con, 12, 12);
        QWidget *c2 = makeColumn(con, 12, 70);
        QWidget *c3 = makeColumn(con, 70, 12);
        QWidget *c4 = makeColumn(con, 12, 12);

        if (tractor) {
            // 前轮在内列（对齐车身前轴轮距），后两桥外/内分列
            addUnitCell(c1, QStringLiteral("Bar"), false, kFirstAfterGap);
            addDataCell(c1, 1, 0, kNormalAfterGap); // 后桥左外
            addDataCell(c1, 2, 0, 0);               // 第三桥左外

            addDataCell(c2, 0, 0, kFirstAfterGap);  // 前桥左
            addDataCell(c2, 1, 2, kNormalAfterGap); // 后桥左内
            addDataCell(c2, 2, 2, 0);               // 第三桥左内

            addDataCell(c3, 0, 1, kFirstAfterGap);  // 前桥右
            addDataCell(c3, 1, 3, kNormalAfterGap); // 后桥右内
            addDataCell(c3, 2, 3, 0);               // 第三桥右内

            addUnitCell(c4, QStringLiteral("℃"), true, kFirstAfterGap);
            addDataCell(c4, 1, 1, kNormalAfterGap); // 后桥右外
            addDataCell(c4, 2, 1, 0);               // 第三桥右外
        } else {
            // 挂车三桥 axle 3/4/5，tire 左外/左内/右内/右外
            const int firstGap = kNormalAfterGap; // CSS .system_tire_con_2 li:first-child mb=12
            addDataCell(c1, 3, 0, firstGap);
            addDataCell(c1, 4, 0, kNormalAfterGap);
            addDataCell(c1, 5, 0, 0);

            addDataCell(c2, 3, 2, firstGap);
            addDataCell(c2, 4, 2, kNormalAfterGap);
            addDataCell(c2, 5, 2, 0);

            addDataCell(c3, 3, 3, firstGap);
            addDataCell(c3, 4, 3, kNormalAfterGap);
            addDataCell(c3, 5, 3, 0);

            addDataCell(c4, 3, 1, firstGap);
            addDataCell(c4, 4, 1, kNormalAfterGap);
            addDataCell(c4, 5, 1, 0);
        }

        row->addWidget(c1, 0, Qt::AlignTop);
        row->addWidget(c2, 0, Qt::AlignTop);
        row->addWidget(c3, 0, Qt::AlignTop);
        row->addWidget(c4, 0, Qt::AlignTop);
        row->addStretch(1);

        con->setProperty("tireBg", QVariant::fromValue(static_cast<QObject *>(bg)));
        con->setProperty("tireBgTop", bgTop);
        con->installEventFilter(this);

        blockLay->addWidget(con);
        return block;
    };

    // 牵引头：h4 margin-top 16；bg center 38px
    auto *tractor = buildBlock(QStringLiteral("牵引头"), 16,
                               QStringLiteral(":/images/pic_setting_tire_qyt.png"), 38, true, true);
    // 挂车：h4 margin-top 20；bg center 36px
    auto *trailer = buildBlock(QStringLiteral("挂车"), 20,
                               QStringLiteral(":/images/pic_setting_tire_gc.png"), 36, false, false);

    // CSS .system_tire { width:600px; margin-left:200px }
    auto *tractorRow = new QHBoxLayout;
    tractorRow->setContentsMargins(kBlockLeft, 0, 0, 0);
    tractorRow->addWidget(tractor, 0, Qt::AlignLeft | Qt::AlignTop);
    tractorRow->addStretch(1);
    wrapLay->addLayout(tractorRow);

    auto *trailerRow = new QHBoxLayout;
    trailerRow->setContentsMargins(kBlockLeft, 0, 0, 0);
    trailerRow->addWidget(trailer, 0, Qt::AlignLeft | Qt::AlignTop);
    trailerRow->addStretch(1);
    wrapLay->addLayout(trailerRow);
    wrapLay->addStretch(1);

    // L / R：CSS .system_tire_wrap>span left:74/893 top:281
    const QString lrStyle =
        QStringLiteral("QLabel{color:#00FAFF;font-size:48px;font-weight:bold;"
                       "background:transparent;border:none;}");
    auto *markL = new QLabel(QStringLiteral("L"), wrap);
    markL->setStyleSheet(lrStyle);
    markL->setFixedSize(48, 72);
    markL->move(74, 281);
    markL->raise();
    auto *markR = new QLabel(QStringLiteral("R"), wrap);
    markR->setStyleSheet(lrStyle);
    markR->setFixedSize(48, 72);
    markR->move(893, 281);
    markR->raise();

    root->addWidget(wrap, 1, Qt::AlignLeft | Qt::AlignTop);
}

bool TirePressurePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show
        || event->type() == QEvent::LayoutRequest) {
        auto *con = qobject_cast<QWidget *>(watched);
        if (con) {
            auto *bg = qobject_cast<QLabel *>(con->property("tireBg").value<QObject *>());
            if (bg) {
                const int bgTop = con->property("tireBgTop").toInt();
                bg->move(qMax(0, (con->width() - bg->width()) / 2), bgTop);
                bg->lower();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString TirePressurePage::pressureText(int kpa)
{
    return QString::number(kpa / 100.0, 'f', 1);
}

QColor TirePressurePage::pressureColor(const QString &alarm, bool stale)
{
    if (stale) {
        return QColor(0x88, 0x88, 0x88);
    }
    if (alarm == QLatin1String("NORMAL")) {
        return QColor(0x00, 0xFF, 0x00);
    }
    if (alarm == QLatin1String("LOW") || alarm == QLatin1String("HIGH")) {
        return QColor(0xFF, 0xD4, 0x00);
    }
    if (alarm == QLatin1String("ULTRA_LOW") || alarm == QLatin1String("ULTRA_HIGH")) {
        return QColor(0xFF, 0x00, 0x00);
    }
    return QColor(0xea, 0xf2, 0xff);
}

void TirePressurePage::applyCell(TireCell *cell, const TireSlot *slot, qint64 nowMs) const
{
    if (!cell || !cell->pressure || !cell->temperature) {
        return;
    }
    if (!slot || !slot->valid) {
        cell->pressure->setText(QStringLiteral("--"));
        stylePressureLabel(cell->pressure, QColor(kColorPressureIdle));
        cell->temperature->setText(QStringLiteral("--"));
        styleTemperatureLabel(cell->temperature, QColor(kColorTemperatureIdle));
        return;
    }

    const bool stale = (nowMs - slot->lastRxMs) > kStaleMs;
    if (stale) {
        // 超时与无数据一致：压力/温度都回 "--"
        cell->pressure->setText(QStringLiteral("--"));
        stylePressureLabel(cell->pressure, QColor(kColorPressureIdle));
        cell->temperature->setText(QStringLiteral("--"));
        styleTemperatureLabel(cell->temperature, QColor(kColorTemperatureStale));
        return;
    }

    cell->pressure->setText(pressureText(slot->info.pressureKpa));
    stylePressureLabel(cell->pressure, pressureColor(slot->info.alarm, false));
    cell->temperature->setText(QString::number(qRound(slot->info.temperatureC)));
    styleTemperatureLabel(cell->temperature, QColor(kColorTemperatureLive));
}

void TirePressurePage::onTpmsReceived(const McuTpmsInfo &info)
{
    if (info.axle < 0 || info.axle > 6 || info.tire < 0 || info.tire > 3) {
        return;
    }
    TireSlot &slot = m_slots[slotKey(info.axle, info.tire)];
    slot.info = info;
    slot.lastRxMs = QDateTime::currentMSecsSinceEpoch();
    slot.valid = true;

    const qint64 now = slot.lastRxMs;
    for (TireCell *cell : m_cells) {
        if (cell->axle == info.axle && cell->tire == info.tire) {
            applyCell(cell, &slot, now);
            break;
        }
    }
}

void TirePressurePage::refreshStale()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (TireCell *cell : m_cells) {
        const auto it = m_slots.constFind(slotKey(cell->axle, cell->tire));
        applyCell(cell, it == m_slots.cend() ? nullptr : &it.value(), now);
    }
}
