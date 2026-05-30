#include "ahdlayout.h"

namespace {

void setVp(AhdViewport *vp, int channel, float x, float y, float w, float h, bool visible)
{
    vp->channel = channel;
    vp->norm = QRectF(x, y, w, h);
    vp->visible = visible;
}

void layout360(AhdViewport out[AhdLayoutSpec::kChannelCount])
{
    setVp(&out[0], 0, 0.0f, 0.0f, 0.5f, 0.5f, true);
    setVp(&out[1], 1, 0.5f, 0.0f, 0.5f, 0.5f, true);
    setVp(&out[2], 2, 0.0f, 0.5f, 0.5f, 0.5f, true);
    setVp(&out[3], 3, 0.5f, 0.5f, 0.5f, 0.5f, true);
}

// 倒车：上左/上右小窗，下后视大窗（对齐 driving_image_driving_behind + 规范图9）
void layout270(AhdViewport out[AhdLayoutSpec::kChannelCount])
{
    setVp(&out[0], 2, 0.0f, 0.0f, 0.5f, 240.0f / 720.0f, true);
    setVp(&out[1], 3, 0.5f, 0.0f, 0.5f, 240.0f / 720.0f, true);
    setVp(&out[2], 1, 0.0f, 240.0f / 720.0f, 1.0f, 472.0f / 720.0f, true);
    setVp(&out[3], 0, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

// 右转向：左侧前/左小窗，右侧大窗（对齐 driving_image_driving_right + 规范图11）
void layout272(AhdViewport out[AhdLayoutSpec::kChannelCount])
{
    const float sideW = 392.0f / 1280.0f;
    setVp(&out[0], 0, 0.0f, 0.0f, sideW, 0.5f, true);
    setVp(&out[1], 2, 0.0f, 0.5f, sideW, 0.5f, true);
    setVp(&out[2], 3, sideW, 0.0f, 1.0f - sideW, 1.0f, true);
    setVp(&out[3], 0, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

// 左转向：左侧大窗，右侧前/右小窗（对齐 driving_image_driving_left + 规范图11）
void layout271(AhdViewport out[AhdLayoutSpec::kChannelCount])
{
    const float sideW = 392.0f / 1280.0f;
    const float mainW = 880.0f / 1280.0f;
    setVp(&out[0], 2, 0.0f, 0.0f, mainW, 1.0f, true);
    setVp(&out[1], 0, mainW, 0.0f, sideW, 0.5f, true);
    setVp(&out[2], 3, mainW, 0.5f, sideW, 0.5f, true);
    setVp(&out[3], 0, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

// 180：行车模式左右二分屏（左/右各半屏，对齐 driving_image_play_driving + 规范图10）
void layout180(AhdViewport out[AhdLayoutSpec::kChannelCount])
{
    setVp(&out[0], 2, 0.0f, 0.0f, 0.5f, 1.0f, true);
    setVp(&out[1], 3, 0.5f, 0.0f, 0.5f, 1.0f, true);
    setVp(&out[2], 0, 0.0f, 0.0f, 0.0f, 0.0f, false);
    setVp(&out[3], 0, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

void layoutSingle(AhdViewport out[AhdLayoutSpec::kChannelCount], int channel)
{
    for (int i = 0; i < AhdLayoutSpec::kChannelCount; ++i) {
        const bool on = (i == channel);
        setVp(&out[i], i, 0.0f, 0.0f, 1.0f, 1.0f, on);
    }
}

} // namespace

QString AhdLayoutSpec::channelLabel(int channel)
{
    switch (channel) {
    case 0:
        return QStringLiteral("前");
    case 1:
        return QStringLiteral("后");
    case 2:
        return QStringLiteral("左");
    case 3:
        return QStringLiteral("右");
    default:
        return QString();
    }
}

void AhdLayoutSpec::viewports(AhdViewport out[kChannelCount]) const
{
    if (fullscreenChannel >= 0 && fullscreenChannel < kChannelCount) {
        layoutSingle(out, fullscreenChannel);
        return;
    }

    switch (mode) {
    case 270:
        layout270(out);
        break;
    case 271:
        layout271(out);
        break;
    case 272:
        layout272(out);
        break;
    case 180:
        layout180(out);
        break;
    case 2:
        layoutSingle(out, 0);
        break;
    case 3:
        layoutSingle(out, 1);
        break;
    case 4:
        layoutSingle(out, 2);
        break;
    case 5:
        layoutSingle(out, 3);
        break;
    case 360:
    default:
        layout360(out);
        break;
    }
}
