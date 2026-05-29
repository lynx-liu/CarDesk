#ifndef AHDLAYOUT_H
#define AHDLAYOUT_H

#include <QRectF>
#include <QString>

// 行车影像显示布局（1280x720 设计稿归一化坐标）
struct AhdViewport {
    int channel = -1; // 0..3 → /dev/video2..5
    QRectF norm;      // 相对预览区域 [0,1]
    bool visible = false;
};

struct AhdLayoutSpec {
    static constexpr int kChannelCount = 4;

    int mode = 360;
    int fullscreenChannel = -1; // 0..3 单路放大，-1 使用 mode 布局

    void viewports(AhdViewport out[kChannelCount]) const;

    // 通道 0..3 → 前/后/左/右（对齐 M93 规范与 ui/driving_image_play*.html）
    static QString channelLabel(int channel);
    static bool isRearChannel(int channel) { return channel == 1; }
};

#endif // AHDLAYOUT_H
