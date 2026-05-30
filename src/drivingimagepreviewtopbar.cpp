#include "drivingimagepreviewtopbar.h"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>

DrivingImagePreviewTopBar::DrivingImagePreviewTopBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("drivingImagePreviewTopBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("background:transparent;border:none;"));

    m_backBtn = new QPushButton(this);
    m_backBtn->setFixedSize(48, 48);
    m_backBtn->setFocusPolicy(Qt::NoFocus);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        QStringLiteral("QPushButton{border:none;background-color:transparent;"
                       "background-image:url(:/images/butt_video_back_up.png);"
                       "background-repeat:no-repeat;background-position:center;outline:none;}"
                       "QPushButton:hover,QPushButton:pressed{"
                       "background-image:url(:/images/butt_video_back_down.png);}"));
    connect(m_backBtn, &QPushButton::clicked, this, &DrivingImagePreviewTopBar::backClicked);

    m_titleLabel = new QLabel(QStringLiteral("行车影像"), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        QStringLiteral("color:#ffffff;font-size:36px;font-weight:700;"
                       "background:transparent;border:none;"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layoutChildren();
}

void DrivingImagePreviewTopBar::setTitle(const QString &title)
{
    if (m_titleLabel) {
        m_titleLabel->setText(title);
    }
}

void DrivingImagePreviewTopBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    static const QPixmap topPix(QStringLiteral(":/images/video_play_top.png"));
    if (!topPix.isNull()) {
        painter.drawPixmap(rect(), topPix);
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, 128));
    }
}

void DrivingImagePreviewTopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void DrivingImagePreviewTopBar::layoutChildren()
{
    if (m_backBtn) {
        m_backBtn->setGeometry(12, 12, 48, 48);
    }
    if (m_titleLabel) {
        m_titleLabel->setGeometry(0, 0, width(), height());
    }
}
