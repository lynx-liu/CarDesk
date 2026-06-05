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

void AhdPreviewOverlayWidget::setChannelFaultTexts(
    const QString texts[AhdLayoutSpec::kChannelCount])
{
    bool changed = false;
    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        if (m_channelFaultTexts[i] != texts[i]) {
            m_channelFaultTexts[i] = texts[i];
            changed = true;
        }
    }
    if (changed) {
        update();
    }
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

        if (m_showRecordingBadge && vp.channel >= 0 && vp.channel < AhdLayoutSpec::kChannelCount) {
            const QString &faultText = m_channelFaultTexts[vp.channel];
            if (!faultText.isEmpty()) {
                QFont faultFont = painter.font();
                faultFont.setPixelSize(qMax(20, dest.height() * 28 / 720));
                faultFont.setBold(true);
                painter.setFont(faultFont);

                const QFontMetrics fm(faultFont);
                const int padH = qMax(12, dest.width() / 40);
                const int padV = qMax(8, dest.height() / 60);
                const int textW = fm.horizontalAdvance(faultText) + padH * 2;
                const int textH = fm.height() + padV * 2;
                QRect faultBg((dest.left() + dest.right() - textW) / 2,
                              (dest.top() + dest.bottom() - textH) / 2, textW, textH);
                painter.fillRect(faultBg, QColor(0, 0, 0, 170));
                painter.setPen(QColor(255, 210, 64));
                painter.drawText(faultBg, Qt::AlignCenter, faultText);
                painter.setFont(font);
                painter.setPen(Qt::white);
            }
        }

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
