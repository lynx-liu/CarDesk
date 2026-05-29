#include "drivingimagesubtopbar.h"

#include "topbarwidget.h"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>

DrivingImageSubTopBar::DrivingImageSubTopBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("drivingImageSubTopBar"));
    setFixedSize(1280, 82);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, true);
    // 与系统设置 / 视频列表等页面完全一致
    setStyleSheet(QStringLiteral("background: url(:/images/topbar.png) no-repeat;"));

    m_homeBtn = new QPushButton(this);
    m_homeBtn->setGeometry(12, 12, 48, 48);
    m_homeBtn->setFocusPolicy(Qt::NoFocus);
    m_homeBtn->setCursor(Qt::PointingHandCursor);
    m_homeBtn->setStyleSheet(
        QStringLiteral("QPushButton{border:none;background-image:url(:/images/pict_home_up.png);"
                       "background-repeat:no-repeat;background-position:center;"
                       "background-color:transparent;}"
                       "QPushButton:hover,QPushButton:pressed{"
                       "background-image:url(:/images/pict_home_down.png);}"));
    connect(m_homeBtn, &QPushButton::clicked, this, &DrivingImageSubTopBar::homeClicked);

    m_titleLabel = new QLabel(QStringLiteral("行车影像"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        QStringLiteral("color:#fff;font-size:36px;font-weight:700;background:transparent;border:none;"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_topBarRight = new TopBarRightWidget(this);
    layoutChildren();
}

void DrivingImageSubTopBar::setTitle(const QString &title)
{
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void DrivingImageSubTopBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    static const QPixmap topBarPix(QStringLiteral(":/images/topbar.png"));
    if (!topBarPix.isNull()) {
        painter.drawPixmap(rect(), topBarPix);
    } else {
        painter.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));
    }
}

void DrivingImageSubTopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DrivingImageSubTopBar::layoutChildren()
{
    const int w = width();
    if (m_titleLabel) {
        m_titleLabel->setGeometry(0, 0, w, 72);
    }
    if (m_topBarRight) {
        m_topBarRight->setGeometry(w - 16 - TopBarRightWidget::preferredWidth(), 17,
                                   TopBarRightWidget::preferredWidth(), 48);
    }
}
