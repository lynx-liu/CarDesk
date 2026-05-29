#include "drivingimagesettingspage.h"

#include "ahdrecordstore.h"
#include "ahdsettings.h"
#include "drivingimagesubtopbar.h"

#include <QButtonGroup>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

void paintModeTileFrame(QPainter &painter, const QRect &rect, bool selected)
{
    const QColor borderColor = selected ? QColor(0, 250, 255) : QColor(0, 104, 255);
    painter.fillRect(rect, QColor(255, 255, 255, 26));
    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
}

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
    QSize targetSize = pixmap.size();
    targetSize.scale(tileRect.width() - 2, tileRect.height() - 2, Qt::KeepAspectRatio);
    const QRect imageRect(
        tileRect.left() + (tileRect.width() - targetSize.width()) / 2,
        tileRect.top() + (tileRect.height() - targetSize.height()) / 2,
        targetSize.width(), targetSize.height());
    painter.drawPixmap(imageRect, pixmap);
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

        const QRect tileRect = rect().adjusted(0, 0, -1, -1);
        const bool selected = isChecked();
        paintModeTileFrame(painter, tileRect, selected);

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

        const QRect tileRect = rect().adjusted(0, 0, -1, -1);
        const bool selected = isChecked();
        paintModeTileFrame(painter, tileRect, selected);

        static const QPixmap sidePix(
            QStringLiteral(":/images/pic_driving_image_setting_direction_side.png"));
        const int halfW = tileRect.width() / 2;
        const QRect leftRect(tileRect.left(), tileRect.top(), halfW, tileRect.height());
        const QRect rightRect(tileRect.left() + halfW, tileRect.top(), tileRect.width() - halfW,
                              tileRect.height());

        paintCenteredPixmap(painter, leftRect, sidePix);
        paintCenteredPixmap(painter, rightRect, sidePix);

        const QColor borderColor = selected ? QColor(0, 250, 255) : QColor(0, 104, 255);
        painter.setPen(QPen(borderColor, 1));
        painter.drawLine(tileRect.left() + halfW, tileRect.top() + 1, tileRect.left() + halfW,
                         tileRect.bottom() - 1);

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

    auto *recRow = new QWidget(modePage);
    recRow->setFixedHeight(96);
    recRow->setStyleSheet(
        QStringLiteral("border-bottom:2px solid rgba(255,255,255,0.1);background:transparent;"));
    auto *recRowLayout = new QHBoxLayout(recRow);
    recRowLayout->setContentsMargins(0, 0, 0, 24);
    auto *recLabel = new QLabel(QStringLiteral("行车录像"), recRow);
    recLabel->setStyleSheet(QStringLiteral("color:#eaf2ff;font-size:32px;background:transparent;"));
    m_recordingSwitch = new QPushButton(recRow);
    m_recordingSwitch->setFixedSize(88, 44);
    m_recordingSwitch->setCheckable(true);
    m_recordingSwitch->setFocusPolicy(Qt::NoFocus);
    m_recordingSwitch->setStyleSheet(
        QStringLiteral("QPushButton{border:none;background-image:url(:/images/butt_setting_close.png);"
                       "background-repeat:no-repeat;background-position:center;}"
                       "QPushButton:checked{background-image:url(:/images/butt_setting_open.png);}"));
    recRowLayout->addWidget(recLabel);
    recRowLayout->addStretch();
    recRowLayout->addWidget(m_recordingSwitch);
    modeLayout->addWidget(recRow);

    auto *modeTitleRow = new QWidget(modePage);
    modeTitleRow->setFixedHeight(96);
    modeTitleRow->setStyleSheet(
        QStringLiteral("border-bottom:2px solid rgba(255,255,255,0.1);background:transparent;"));
    auto *modeTitleLayout = new QHBoxLayout(modeTitleRow);
    modeTitleLayout->setContentsMargins(0, 0, 0, 24);
    auto *modeLabel = new QLabel(QStringLiteral("行车模式"), modeTitleRow);
    modeLabel->setStyleSheet(QStringLiteral("color:#eaf2ff;font-size:32px;background:transparent;"));
    modeTitleLayout->addWidget(modeLabel);
    modeTitleLayout->addStretch();
    modeLayout->addWidget(modeTitleRow);

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
    formatLayout->setContentsMargins(0, 24, 0, 0);
    auto *formatHint = new QLabel(
        QStringLiteral("格式化将删除存储卡中的行车录像文件\n请谨慎操作"), formatPage);
    formatHint->setAlignment(Qt::AlignCenter);
    formatHint->setWordWrap(true);
    formatHint->setStyleSheet(QStringLiteral("color:#eaf2ff;font-size:32px;background:transparent;"));
    auto *formatBtn = new QPushButton(QStringLiteral("开始格式化"), formatPage);
    formatBtn->setFixedSize(192, 54);
    formatBtn->setStyleSheet(
        QStringLiteral("QPushButton{background:transparent;color:#fff;font-size:24px;"
                       "border:2px solid #0068FF;}"
                       "QPushButton:hover{color:#00FAFF;border-color:#00FAFF;}"));
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
        AhdSettings::instance().setRecordingEnabled(on);
        emit recordingToggled(on);
    });

    connect(m_modeRear, &QPushButton::toggled, this, [this](bool) { m_modeRear->update(); });
    connect(m_modeLeftRight, &QPushButton::toggled, this, [this](bool) { m_modeLeftRight->update(); });
    connect(m_modeRear, &QPushButton::clicked, this, [this]() {
        AhdSettings::instance().setPreferredDrivingMode(270);
        refreshModeTiles();
        emit requestApplyDrivingMode(270);
    });
    connect(m_modeLeftRight, &QPushButton::clicked, this, [this]() {
        AhdSettings::instance().setPreferredDrivingMode(271);
        refreshModeTiles();
        emit requestApplyDrivingMode(271);
    });

    connect(formatBtn, &QPushButton::clicked, this, &DrivingImageSettingsPage::onFormatClicked);
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

void DrivingImageSettingsPage::onFormatClicked()
{
    const auto reply = QMessageBox::question(
        this, QStringLiteral("格式化"),
        QStringLiteral("确定删除存储卡中所有行车录像文件吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }
    QString msg;
    if (AhdRecordStore::formatStorage(&msg)) {
        QMessageBox::information(this, QStringLiteral("格式化"), msg);
    } else {
        QMessageBox::warning(this, QStringLiteral("格式化"), msg);
    }
}
