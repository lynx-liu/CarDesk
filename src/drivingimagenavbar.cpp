#include "drivingimagenavbar.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>

namespace {

QString drivingNavBtnStyle(const char *up, const char *down)
{
    return QStringLiteral(
               "QPushButton{border:none;background-color:transparent;"
               "background-image:url(:/images/%1);"
               "background-repeat:no-repeat;background-position:center;outline:none;}"
               "QPushButton:checked,QPushButton:hover,QPushButton:pressed{"
               "background-image:url(:/images/%2);}")
        .arg(QLatin1String(up), QLatin1String(down));
}

} // namespace

DrivingImageNavBar::DrivingImageNavBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("drivingImageNavBar"));
    setFixedHeight(108);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("background:transparent;border:none;"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(385, 24, 385, 24);
    layout->setSpacing(165);

    m_btnPreview = new QPushButton(this);
    m_btnSettings = new QPushButton(this);
    m_btnPlayback = new QPushButton(this);

    for (QPushButton *btn : {m_btnPreview, m_btnSettings, m_btnPlayback}) {
        btn->setFixedSize(60, 60);
        btn->setCheckable(true);
        btn->setFlat(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
    }

    m_btnPreview->setStyleSheet(drivingNavBtnStyle("butt_driving_image_video_up.png",
                                                    "butt_driving_image_video_down.png"));
    m_btnSettings->setStyleSheet(drivingNavBtnStyle("butt_driving_image_setting_up.png",
                                                     "butt_driving_image_setting_down.png"));
    m_btnPlayback->setStyleSheet(drivingNavBtnStyle("butt_driving_image_playback_up.png",
                                                     "butt_driving_image_playback_down.png"));

    layout->addWidget(m_btnPreview);
    layout->addWidget(m_btnSettings);
    layout->addWidget(m_btnPlayback);

    connect(m_btnPreview, &QPushButton::clicked, this, [this]() { emit tabSelected(TabPreview); });
    connect(m_btnSettings, &QPushButton::clicked, this, [this]() { emit tabSelected(TabSettings); });
    connect(m_btnPlayback, &QPushButton::clicked, this, [this]() { emit tabSelected(TabPlayback); });

    setActiveTab(TabPreview);
}

void DrivingImageNavBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    static const QPixmap bottomPix(QStringLiteral(":/images/pict_driving_image_bottom.png"));
    if (!bottomPix.isNull()) {
        painter.drawPixmap(rect(), bottomPix);
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, 128));
    }
}

void DrivingImageNavBar::setActiveTab(Tab tab)
{
    m_btnPreview->setChecked(tab == TabPreview);
    m_btnSettings->setChecked(tab == TabSettings);
    m_btnPlayback->setChecked(tab == TabPlayback);
}
