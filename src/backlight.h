#pragma once

// Allwinner dispdbg 背光控制工具
// 同时被 main.cpp 的 ScreenBlanker 和 SystemSettingWindow 使用

namespace Backlight {

    // 读取当前亮度（先取缓存，再尝试 sysfs，失败返回 180）
    int get();

    // 通过 dispdbg 设置亮度（0-255），同时更新缓存
    void set(int value);

    // slider(0-100) ↔ backlight(10-255) 转换
    // slider=0 → bl=10（最暗但不熄屏），slider=100 → bl=255（最亮）
    int sliderToBacklight(int sliderVal);
    int backlightToSlider(int blVal);

} // namespace Backlight
