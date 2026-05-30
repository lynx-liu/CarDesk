#include "drivingimagesettingspage.h"

#include "ahdrecordstore.h"
#include "ahdsettings.h"
#include "drivingimagesubtopbar.h"

#include <QButtonGroup>
#include <QEventLoop>
#include <QFontMetrics>
#include <QMainWindow>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

void paintTopRightLabel(QPainter &painter, const QRect &tileRect, const QString &text, bool selected)
{
    QFont font = painter.font();
    font.setPixelSize(24);
    painter.setFont(font);
    painter.setPen(selected ? QColor(0, 250, 255) : QColor(255, 255, 255));
    const QFontMetrics fm(font);
    const int labelH = fm.height() + 8;
    const QRect labelRect(tileRect.left() + 8, tileRect.top() + 8, tileRect.width() - 16, labelH);
    painter.drawText(labelRect, Qt::AlignRight | Qt::AlignTop, text);
}

void paintBottomRightCheck(QPainter &painter, const QRect &tileRect, bool selected)
{
    static const QPixmap checkOn(QStringLiteral(":/images/butt_setting_check_down.png"));
    static const QPixmap checkOff(QStringLiteral(":/images/butt_setting_check_up.png"));
    const QPixmap &checkPix = selected ? checkOn : checkOff;
    if (!checkPix.isNull()) {
        painter.drawPixmap(tileRect.right() - 36 - 8 + 1, tileRect.bottom() - 36 - 8 + 1, checkPix);
    }
}

void paintCenteredPixmap(QPainter &painter, const QRect &tileRect, const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return;
    }
    painter.drawPixmap(tileRect, pixmap);
}

class ModeSelectButton : public QPushButton {
public:
    ModeSelectButton(const QString &label, const QString &bgImage, int width, int height,
                     QWidget *parent = nullptr)
        : QPushButton(parent)
        , m_label(label)
        , m_bgImage(bgImage)
    {
        setCheckable(true);
        setFlat(true);
        setText(QString());
        setFixedSize(width, height);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;outline:none;}"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QRect tileRect = rect();
        const bool selected = isChecked();
        static QPixmap bgPix;
        if (bgPix.isNull()) {
            bgPix = QPixmap(m_bgImage);
        }
        paintCenteredPixmap(painter, tileRect, bgPix);
        paintTopRightLabel(painter, tileRect, m_label, selected);
        paintBottomRightCheck(painter, tileRect, selected);
    }

private:
    QString m_label;
    QString m_bgImage;
};

// 左右：312x232 单模式，中间竖线分隔（对齐 UI 原型 setting_direction）
class LeftRightModeButton : public QPushButton {
public:
    explicit LeftRightModeButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setFlat(true);
        setText(QString());
        setFixedSize(312, 232);
        setFocusPolicy(Qt::NoFocus);
        setStyleSheet(QStringLiteral("QPushButton{border:none;background:transparent;outline:none;}"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        const QRect tileRect = rect();
        const bool selected = isChecked();

        static const QPixmap sidePix(
            QStringLiteral(":/images/pic_driving_image_setting_direction_side.png"));
        const int halfW = tileRect.width() / 2;
        const QRect leftRect(tileRect.left(), tileRect.top(), halfW, tileRect.height());
        const QRect rightRect(tileRect.left() + halfW, tileRect.top(), tileRect.width() - halfW,
                              tileRect.height());

        paintCenteredPixmap(painter, leftRect, sidePix);
        paintCenteredPixmap(painter, rightRect, sidePix);

        paintTopRightLabel(painter, leftRect, QStringLiteral("左"), selected);
        paintTopRightLabel(painter, rightRect, QStringLiteral("右"), selected);
        paintBottomRightCheck(painter, tileRect, selected);
    }
};

} // namespace

DrivingImageSettingsPage::DrivingImageSettingsPage(AhdManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    setObjectName(QStringLiteral("drivingImageSettingsPage"));
    setStyleSheet(QStringLiteral("#drivingImageSettingsPage{background:transparent;border:none;}"));
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setupUI();
    refreshRecordingSwitch();
    refreshModeTiles();
}

void DrivingImageSettingsPage::setupUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 108);
    root->setSpacing(0);

    m_topBar = new DrivingImageSubTopBar(this);
    m_topBar->setTitle(QStringLiteral("行车影像"));
    connect(m_topBar, &DrivingImageSubTopBar::homeClicked, this,
            &DrivingImageSettingsPage::requestReturnToMain);
    root->addWidget(m_topBar);

    auto *bodyWrap = new QWidget(this);
    bodyWrap->setObjectName(QStringLiteral("drivingImageSettingsBody"));
    bodyWrap->setStyleSheet(QStringLiteral("#drivingImageSettingsBody{background:transparent;}"));
    bodyWrap->setMinimumSize(0, 0);
    bodyWrap->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *body = new QHBoxLayout(bodyWrap);
    body->setContentsMargins(0, 24, 0, 0);
    body->setSpacing(0);

    m_subnav = new QListWidget(bodyWrap);
    m_subnav->setFixedWidth(280);
    m_subnav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *navMode = new QListWidgetItem(QStringLiteral("行车模式"), m_subnav);
    navMode->setTextAlignment(Qt::AlignCenter);
    auto *navFormat = new QListWidgetItem(QStringLiteral("格式化"), m_subnav);
    navFormat->setTextAlignment(Qt::AlignCenter);
    m_subnav->setStyleSheet(
        QStringLiteral("QListWidget{border:none;background:rgba(0,0,0,0.22);font-size:32px;"
                       "outline:none;color:#eaf2ff;}"
                       "QListWidget::item{height:90px;padding:0;color:#eaf2ff;}"
                       "QListWidget::item:selected{background-image:url(:/images/butt_subnav_on.png);"
                       "background-repeat:no-repeat;background-position:center;color:#00faff;}"
                       "QListWidget::item:hover{background-image:url(:/images/butt_subnav_on.png);"
                       "background-repeat:no-repeat;background-position:center;color:#00faff;}"));
    m_subnav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_subnav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(m_subnav);

    auto *pageHost = new QWidget(bodyWrap);
    pageHost->setObjectName(QStringLiteral("drivingImageSettingsHost"));
    pageHost->setStyleSheet(QStringLiteral("#drivingImageSettingsHost{background:transparent;}"));
    pageHost->setMinimumSize(0, 0);
    pageHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *pageHostLayout = new QVBoxLayout(pageHost);
    pageHostLayout->setContentsMargins(80, 0, 80, 0);
    pageHostLayout->setSpacing(0);

    m_stack = new QStackedWidget(pageHost);
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget{background:transparent;border:none;}"));

    auto *modePage = new QWidget(m_stack);
    modePage->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *modeLayout = new QVBoxLayout(modePage);
    modeLayout->setContentsMargins(0, 24, 0, 0);
    modeLayout->setSpacing(0);

    auto *recRowWrap = new QWidget(modePage);
    recRowWrap->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *recWrapLayout = new QVBoxLayout(recRowWrap);
    recWrapLayout->setContentsMargins(0, 0, 0, 24);
    recWrapLayout->setSpacing(0);

    auto *recRow = new QWidget(recRowWrap);
    recRow->setFixedHeight(72);
    recRow->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *recRowLayout = new QHBoxLayout(recRow);
    recRowLayout->setContentsMargins(0, 0, 0, 0);
    auto *recLabel = new QLabel(QStringLiteral("行车录像"), recRow);
    recLabel->setStyleSheet(QStringLiteral("color:#eaf2ff;font-size:32px;background:transparent;"));
    m_recordingSwitch = new QPushButton(recRow);
    m_recordingSwitch->setFixedSize(88, 44);
    m_recordingSwitch->setCheckable(true);
    m_recordingSwitch->setFocusPolicy(Qt::NoFocus);
    m_recordingSwitch->setStyleSheet(
        QStringLiteral("QPushButton{border:none;background-image:url(:/images/butt_setting_close.png);"
                       "background-repeat:no-repeat;background-position:center;}"
                       "QPushButton:checked{background-image:url(:/images/butt_setting_open.png);}"
                       "QPushButton:disabled{background-image:url(:/images/butt_setting_close.png);}"));
    recRowLayout->addWidget(recLabel);
    recRowLayout->addStretch();
    recRowLayout->addWidget(m_recordingSwitch);
    recWrapLayout->addWidget(recRow);

    auto *recDivider = new QFrame(recRowWrap);
    recDivider->setFixedHeight(2);
    recDivider->setStyleSheet(QStringLiteral("background:rgba(255,255,255,26);border:none;"));
    recWrapLayout->addWidget(recDivider);
    modeLayout->addWidget(recRowWrap);

    auto *modeTitleRow = new QWidget(modePage);
    modeTitleRow->setFixedHeight(72);
    modeTitleRow->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *modeTitleLayout = new QHBoxLayout(modeTitleRow);
    modeTitleLayout->setContentsMargins(0, 0, 0, 0);
    auto *modeLabel = new QLabel(QStringLiteral("行车模式"), modeTitleRow);
    modeLabel->setStyleSheet(QStringLiteral("color:#eaf2ff;font-size:32px;background:transparent;"));
    modeTitleLayout->addWidget(modeLabel);
    modeTitleLayout->addStretch();
    modeLayout->addWidget(modeTitleRow);

    auto *sectionDividerWrap = new QWidget(modePage);
    sectionDividerWrap->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *sectionDividerLayout = new QVBoxLayout(sectionDividerWrap);
    sectionDividerLayout->setContentsMargins(0, 0, 0, 24);
    sectionDividerLayout->setSpacing(0);
    auto *sectionDivider = new QFrame(sectionDividerWrap);
    sectionDivider->setFixedHeight(2);
    sectionDivider->setStyleSheet(QStringLiteral("background:rgba(255,255,255,26);border:none;"));
    sectionDividerLayout->addWidget(sectionDivider);
    modeLayout->addWidget(sectionDividerWrap);

    auto *tilesRow = new QHBoxLayout();
    tilesRow->setContentsMargins(76, 0, 0, 0);
    tilesRow->setSpacing(0);

    m_modeRear = new ModeSelectButton(
        QStringLiteral("后"),
        QStringLiteral(":/images/pic_driving_image_setting_direction_behind.png"), 312, 232, modePage);
    m_modeLeftRight = new LeftRightModeButton(modePage);

    tilesRow->addWidget(m_modeRear);
    tilesRow->addSpacing(72);
    tilesRow->addWidget(m_modeLeftRight);
    tilesRow->addStretch();
    modeLayout->addLayout(tilesRow);
    modeLayout->addStretch();

    auto *formatPage = new QWidget(m_stack);
    formatPage->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    auto *formatLayout = new QVBoxLayout(formatPage);
    formatLayout->setContentsMargins(0, 138, 0, 0);
    auto *formatHint = new QLabel(
        QStringLiteral("格式化将删除\"TF卡\"中的所有数据\n请谨慎操作"), formatPage);
    formatHint->setAlignment(Qt::AlignCenter);
    formatHint->setWordWrap(true);
    formatHint->setStyleSheet(
        QStringLiteral("color:#eaf2ff;font-size:32px;line-height:48px;background:transparent;"));
    auto *formatBtn = new QPushButton(QStringLiteral("开始格式化"), formatPage);
    formatBtn->setFixedSize(192, 54);
    formatBtn->setFocusPolicy(Qt::NoFocus);
    formatBtn->setStyleSheet(
        QStringLiteral("QPushButton{background:transparent;color:#fff;font-size:24px;"
                       "border:2px solid #0068FF;outline:none;}"
                       "QPushButton:hover{color:#00FAFF;border:2px solid #00FAFF;}"
                       "QPushButton:disabled{color:#666666;border:2px solid #666666;}"
                       "QPushButton:focus,QPushButton:pressed{outline:none;border:2px solid #0068FF;}"));
    m_formatBtn = formatBtn;
    formatLayout->addWidget(formatHint, 0, Qt::AlignHCenter);
    formatLayout->addSpacing(55);
    formatLayout->addWidget(formatBtn, 0, Qt::AlignHCenter);
    formatLayout->addStretch();

    m_stack->addWidget(modePage);
    m_stack->addWidget(formatPage);
    pageHostLayout->addWidget(m_stack);
    body->addWidget(pageHost, 1);
    root->addWidget(bodyWrap, 1);

    connect(m_subnav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_subnav->setCurrentRow(0);

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_modeRear);
    modeGroup->addButton(m_modeLeftRight);

    connect(m_recordingSwitch, &QPushButton::toggled, this, [this](bool on) {
        if (!AhdRecordStore::hasRecordStorage()) {
            refreshRecordingSwitch();
            return;
        }
        AhdSettings::instance().setRecordingEnabled(on);
        emit recordingToggled(on);
    });

    connect(m_modeRear, &QPushButton::toggled, this, [this](bool) { m_modeRear->update(); });
    connect(m_modeLeftRight, &QPushButton::toggled, this, [this](bool) { m_modeLeftRight->update(); });
    connect(m_modeRear, &QPushButton::clicked, this, [this]() {
        AhdSettings::instance().setPreferredDrivingMode(270);
        refreshModeTiles();
    });
    connect(m_modeLeftRight, &QPushButton::clicked, this, [this]() {
        AhdSettings::instance().setPreferredDrivingMode(271);
        refreshModeTiles();
    });

    connect(formatBtn, &QPushButton::clicked, this, &DrivingImageSettingsPage::onFormatClicked);

    refreshStorageState();
}

bool DrivingImageSettingsPage::showPopAlert(QWidget *parent, const QString &title, bool withCancel)
{
    QWidget *host = parent ? parent->window() : nullptr;
    if (!host) {
        return false;
    }
    QWidget *surface = host;
    if (auto *mainWin = qobject_cast<QMainWindow *>(host)) {
        if (QWidget *central = mainWin->centralWidget()) {
            surface = central;
        }
    }

    auto *layer = new QWidget(surface);
    layer->setObjectName(QStringLiteral("popAlertLayer"));
    layer->setGeometry(surface->rect());
    layer->setAttribute(Qt::WA_StyledBackground, true);
    layer->setStyleSheet(QStringLiteral("background:transparent;"));
    layer->show();
    layer->raise();

    auto *panel = new QWidget(layer);
    panel->setObjectName(QStringLiteral("popAlertPanel"));
    panel->setFixedSize(530, 272);
    panel->move((layer->width() - panel->width()) / 2, (layer->height() - panel->height()) / 2);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setAutoFillBackground(true);
    panel->setStyleSheet(
        QStringLiteral("QWidget#popAlertPanel{background-color:#1a2950;"
                       "border-image:url(:/images/pict_popalert_bg.png) 25 25 25 25 stretch stretch;"
                       "border:25px solid transparent;}"));
    panel->show();
    panel->raise();

    auto *root = new QVBoxLayout(panel);
    root->setContentsMargins(28, 28, 28, 28);
    root->setSpacing(12);

    auto *top = new QLabel(title, panel);
    top->setMinimumHeight(104);
    top->setWordWrap(true);
    top->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    top->setStyleSheet(
        QStringLiteral("color:#ffffff;font-size:36px;line-height:96px;padding-left:124px;"
                       "background:transparent url(:/images/pict_popalert_icon.png) left top no-repeat;"
                       "border:none;"));

    auto *btnWrap = new QWidget(panel);
    auto *btnRow = new QHBoxLayout(btnWrap);
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(0);
    auto *okBtn = new QPushButton(QStringLiteral("确认"), btnWrap);
    okBtn->setFixedSize(168, 64);
    okBtn->setFocusPolicy(Qt::NoFocus);
    okBtn->setStyleSheet(
        QStringLiteral("QPushButton{background:none;color:#ffffff;font-size:36px;"
                       "border:2px solid #0068FF;outline:none;}"
                       "QPushButton:hover{color:#00FAFF;border:2px solid #00FAFF;}"));
    QPushButton *cancelBtn = nullptr;
    if (withCancel) {
        btnWrap->setFixedWidth(392);
        btnRow->addStretch();
        btnRow->addWidget(okBtn);
        btnRow->addSpacing(56);
        cancelBtn = new QPushButton(QStringLiteral("取消"), btnWrap);
        cancelBtn->setFixedSize(168, 64);
        cancelBtn->setFocusPolicy(Qt::NoFocus);
        cancelBtn->setStyleSheet(
            QStringLiteral("QPushButton{background:none;color:#ffffff;font-size:36px;"
                           "border:2px solid #999999;outline:none;}"
                           "QPushButton:hover{color:#cccccc;border:2px solid #999999;}"));
        btnRow->addWidget(cancelBtn);
        btnRow->addStretch();
    } else {
        btnWrap->setFixedWidth(168);
        btnRow->addStretch();
        btnRow->addWidget(okBtn);
        btnRow->addStretch();
    }

    root->addWidget(top);
    auto *btnCenter = new QHBoxLayout();
    btnCenter->addStretch();
    btnCenter->addWidget(btnWrap);
    btnCenter->addStretch();
    root->addLayout(btnCenter);

    bool accepted = false;
    QEventLoop loop;
    const auto finish = [layer, &accepted, &loop](bool ok) {
        accepted = ok;
        layer->deleteLater();
        loop.quit();
    };
    connect(okBtn, &QPushButton::clicked, layer, [finish]() { finish(true); });
    if (cancelBtn) {
        connect(cancelBtn, &QPushButton::clicked, layer, [finish]() { finish(false); });
    }

    loop.exec();
    return accepted;
}

void DrivingImageSettingsPage::refreshRecordingSwitch()
{
    if (!m_recordingSwitch) {
        return;
    }
    const bool on = AhdSettings::instance().recordingEnabled();
    m_recordingSwitch->blockSignals(true);
    m_recordingSwitch->setChecked(on);
    m_recordingSwitch->blockSignals(false);
}

void DrivingImageSettingsPage::refreshModeTiles()
{
    const int mode = AhdSettings::instance().preferredDrivingMode();
    m_modeRear->setChecked(mode == 270);
    m_modeLeftRight->setChecked(mode == 271);
    m_modeRear->update();
    m_modeLeftRight->update();
}

void DrivingImageSettingsPage::refreshStorageState()
{
    const bool hasTf = AhdRecordStore::hasRecordStorage();
    if (m_formatBtn) {
        m_formatBtn->setEnabled(hasTf);
    }
    if (m_recordingSwitch) {
        m_recordingSwitch->setEnabled(hasTf);
    }
}

void DrivingImageSettingsPage::onFormatClicked()
{
    if (!AhdRecordStore::hasRecordStorage()) {
        return;
    }
    if (!showPopAlert(this, QStringLiteral("您确定执行此操作吗"), true)) {
        return;
    }
    QString msg;
    if (AhdRecordStore::formatStorage(&msg)) {
        showPopAlert(this, msg.isEmpty() ? QStringLiteral("格式化完成") : msg, false);
    } else {
        showPopAlert(this, msg.isEmpty() ? QStringLiteral("格式化失败") : msg, false);
    }
}
