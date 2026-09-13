// ESP32-S3 显示后端: Arduino_GFX(ILI9488 480x320) + LVGL 显示驱动。
// ⚠ 按你的板子改下面的引脚。
#include "esp_display.h"
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// ---- SPI 引脚(默认 ILI9488 3.5" 屏; 按接线改) ----
#define TFT_SCK  12
#define TFT_MOSI 11
#define TFT_MISO -1
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14
#define TFT_BL   2

// 分辨率必须是编译期常量: LVGL 8.3 的 LV_HOR_RES/LV_VER_RES 是
// lv_disp_get_hor_res(lv_disp_get_default()), 显示驱动注册之前返回 0,
// 用它做数组长度既编不过, 也会把显示器注册成 0x0。
#define SCREEN_W 480
#define SCREEN_H 320

static Arduino_DataBus* g_bus = nullptr;
static Arduino_GFX*     g_gfx = nullptr;

static void lv_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    if (g_gfx) {
        g_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p,
                                  area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
    }
    lv_disp_flush_ready(drv);
}

bool display_init() {
    g_bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
    g_gfx = new Arduino_ILI9488_18bit(g_bus, TFT_RST);
    g_gfx->begin();
    g_gfx->setRotation(1);            // 横屏 480x320
    g_gfx->fillScreen(BLACK);

    if (TFT_BL >= 0) {
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, HIGH);
    }

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[SCREEN_W * 40];     // 部分刷新缓冲, 省内存
    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, SCREEN_W * 40);

    static lv_disp_drv_t disp;
    lv_disp_drv_init(&disp);
    disp.hor_res  = SCREEN_W;
    disp.ver_res  = SCREEN_H;
    disp.flush_cb = lv_flush_cb;
    disp.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp);
    return true;
}
