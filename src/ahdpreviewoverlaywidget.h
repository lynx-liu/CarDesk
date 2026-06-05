#ifndef AHDPREVIEWOVERLAYWIDGET_H
#define AHDPREVIEWOVERLAYWIDGET_H

#include <QWidget>

#include "ahdlayout.h"

// 叠在 AhdPreviewGLWidget 之上的普通 QWidget，用 QPainter 画角标，避免 QOpenGLWidget 上绘字变形。
class AhdPreviewOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit AhdPreviewOverlayWidget(QWidget *parent = nullptr);

    void setLayoutSpec(const AhdLayoutSpec &spec);
    void setShowRecordingBadge(bool show);
    void setChannelFaultTexts(const QString texts[AhdLayoutSpec::kChannelCount]);
    void setShowChannelFps(bool show);
    void setChannelFpsValues(const double fps[AhdLayoutSpec::kChannelCount]);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    AhdLayoutSpec m_layout;
    bool m_showRecordingBadge = false;
    bool m_showChannelFps = false;
    QString m_channelFaultTexts[AhdLayoutSpec::kChannelCount];
    double m_channelFps[AhdLayoutSpec::kChannelCount] = {};
};

#endif // AHDPREVIEWOVERLAYWIDGET_H
