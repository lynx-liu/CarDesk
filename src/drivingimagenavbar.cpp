#include "drivingimagenavbar.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>

DrivingImageNavBar::DrivingImageNavBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(108);
    setStyleSheet(QStringLiteral("QWidget{background:rgba(0,0,0,128);border:none;}"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(385, 0, 385, 0);
    layout->setSpacing(0);

    m_btnPreview = new QPushButton(this);
    m_btnSettings = new QPushButton(this);
    m_btnPlayback = new QPushButton(this);

    const QString btnStyle =
        QStringLiteral("QPushButton{border:none;background:transparent;min-width:170px;"
                         "min-height:108px;}"
                         "QPushButton:checked{border:none;}");
    for (QPushButton *btn : {m_btnPreview, m_btnSettings, m_btnPlayback}) {
        btn->setCheckable(true);
        btn->setFlat(true);
        btn->setStyleSheet(btnStyle);
        btn->setFocusPolicy(Qt::NoFocus);
    }

    m_btnPreview->setIcon(QIcon(QStringLiteral(":/images/butt_driving_image_video_up.png")));
    m_btnPreview->setIconSize(QSize(60, 60));
    m_btnSettings->setIcon(QIcon(QStringLiteral(":/images/butt_driving_image_setting_up.png")));
    m_btnSettings->setIconSize(QSize(60, 60));
    m_btnPlayback->setIcon(QIcon(QStringLiteral(":/images/butt_driving_image_playback_up.png")));
    m_btnPlayback->setIconSize(QSize(60, 60));

    layout->addWidget(m_btnPreview);
    layout->addStretch(1);
    layout->addWidget(m_btnSettings);
    layout->addStretch(1);
    layout->addWidget(m_btnPlayback);

    connect(m_btnPreview, &QPushButton::clicked, this, [this]() { emit tabSelected(TabPreview); });
    connect(m_btnSettings, &QPushButton::clicked, this, [this]() { emit tabSelected(TabSettings); });
    connect(m_btnPlayback, &QPushButton::clicked, this, [this]() { emit tabSelected(TabPlayback); });

    setActiveTab(TabPreview);
}

void DrivingImageNavBar::setActiveTab(Tab tab)
{
    m_btnPreview->setChecked(tab == TabPreview);
    m_btnSettings->setChecked(tab == TabSettings);
    m_btnPlayback->setChecked(tab == TabPlayback);

    m_btnPreview->setIcon(QIcon(tab == TabPreview
                                    ? QStringLiteral(":/images/butt_driving_image_video_down.png")
                                    : QStringLiteral(":/images/butt_driving_image_video_up.png")));
    m_btnSettings->setIcon(QIcon(tab == TabSettings
                                     ? QStringLiteral(":/images/butt_driving_image_setting_down.png")
                                     : QStringLiteral(":/images/butt_driving_image_setting_up.png")));
    m_btnPlayback->setIcon(QIcon(tab == TabPlayback
                                     ? QStringLiteral(":/images/butt_driving_image_playback_down.png")
                                     : QStringLiteral(":/images/butt_driving_image_playback_up.png")));
}
