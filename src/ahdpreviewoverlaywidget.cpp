#include "ahdpreviewoverlaywidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>

AhdPreviewOverlayWidget::AhdPreviewOverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background:transparent;border:none;"));
}

void AhdPreviewOverlayWidget::setLayoutSpec(const AhdLayoutSpec &spec)
{
    m_layout = spec;
    update();
}

void AhdPreviewOverlayWidget::setShowRecordingBadge(bool show)
{
    if (m_showRecordingBadge == show) {
        return;
    }
    m_showRecordingBadge = show;
    update();
}

void AhdPreviewOverlayWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (width() < 8 || height() < 8) {
        return;
    }

    AhdViewport viewports[AhdLayoutSpec::kChannelCount];
    m_layout.viewports(viewports);

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font = painter.font();
    font.setPixelSize(qMax(22, height() * 30 / 720));
    font.setBold(true);
    painter.setFont(font);

    const int badge = qMax(36, height() * 48 / 720);
    QRect recRect;

    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        const AhdViewport &vp = viewports[i];
        if (!vp.visible || vp.channel < 0) {
            continue;
        }

        const QString label = AhdLayoutSpec::channelLabel(vp.channel);
        if (label.isEmpty()) {
            continue;
        }

        const QRect dest(static_cast<int>(vp.norm.left() * width()),
                         static_cast<int>(vp.norm.top() * height()),
                         static_cast<int>(vp.norm.width() * width()),
                         static_cast<int>(vp.norm.height() * height()));
        if (dest.width() < 8 || dest.height() < 8) {
            continue;
        }

        const QRect badgeRect(dest.right() - badge + 1, dest.top(), badge, badge);
        painter.fillRect(badgeRect, QColor(0, 0, 0, 128));
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, label);

        if (m_showRecordingBadge && AhdLayoutSpec::isRearChannel(vp.channel)) {
            const int iconSide = qMax(24, badge * 2 / 3);
            recRect = QRect(badgeRect.left() - iconSide - 6, badgeRect.top() + 6, iconSide, iconSide);
        }
    }

    if (m_showRecordingBadge && !recRect.isEmpty()) {
        static const QPixmap recIcon(
            QStringLiteral(":/images/pict_driving_image_video_recording.png"));
        if (!recIcon.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QPixmap scaled =
                recIcon.scaled(recRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawPixmap(recRect.topLeft(), scaled);
        }
    }
}
