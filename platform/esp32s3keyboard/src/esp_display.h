#pragma once
namespace esp_hal {
bool display_init();              // TFT_eSPI(NV3007) + 注册 LVGL 显示驱动
void display_set_backlight(int percent);   // 0..100
} // namespace esp_hal
