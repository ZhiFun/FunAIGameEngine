// ESP32-S3 显示后端(键盘硬件实例): TFT_eSPI + NV3007 428x142。
// 引脚/控制器在 include/nv3007_setup.h 里配置。
#include "esp_display.h"
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// 分辨率必须是编译期常量: LVGL 的 LV_HOR_RES/LV_VER_RES 是运行时宏
// (lv_disp_get_hor_res(lv_disp_get_default())), 注册显示之前返回 0。
#define SCREEN_W 428
#define SCREEN_H 142
#define DISP_BUF_SIZE (SCREEN_W * SCREEN_H)

namespace {

TFT_eSPI tft;
lv_disp_draw_buf_t s_draw_buf;
lv_color_t* s_buf1 = nullptr;
lv_color_t* s_buf2 = nullptr;

// 背光 PWM(GPIO37, 低电平点亮 -> 占空比取反)
constexpr uint8_t  kBlPwmChannel = 6;
constexpr uint32_t kBlPwmFreq    = 5000;
constexpr uint8_t  kBlPwmBits    = 12;
bool s_bl_ready = false;

void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

} // namespace

void esp_hal::display_set_backlight(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (!s_bl_ready) {
        pinMode(TFT_BL, OUTPUT);
        if (ledcSetup(kBlPwmChannel, kBlPwmFreq, kBlPwmBits) > 0.0) {
            ledcAttachPin(TFT_BL, kBlPwmChannel);
            s_bl_ready = true;
        }
    }

#if defined(TFT_BACKLIGHT_ON) && (TFT_BACKLIGHT_ON == LOW)
    const uint32_t max_duty = (1UL << kBlPwmBits) - 1UL;
    const uint32_t duty = max_duty - (uint32_t)percent * max_duty / 100UL;  // 低电平点亮
#else
    const uint32_t max_duty = (1UL << kBlPwmBits) - 1UL;
    const uint32_t duty = (uint32_t)percent * max_duty / 100UL;
#endif

    if (s_bl_ready) {
        ledcWrite(kBlPwmChannel, duty);
    } else {
        digitalWrite(TFT_BL, percent > 0 ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
    }
}

bool esp_hal::display_init() {
    tft.begin();
    tft.setRotation(1);          // 428x142 横屏
    tft.fillScreen(TFT_BLACK);
    display_set_backlight(100);

    // 全屏双缓冲放 PSRAM: 每帧只推 1~2 块, 比小缓冲分片推快得多。
    // TFT_eSPI 这里是同步推送, 所以 PSRAM 缓冲不会和 DMA 打架。
    const size_t bytes = (size_t)DISP_BUF_SIZE * sizeof(lv_color_t);
    s_buf1 = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buf1 == nullptr || s_buf2 == nullptr) {
        Serial.printf("LVGL PSRAM draw buffer alloc failed (%u bytes x2), psram_free=%u\n",
                      (unsigned)bytes, (unsigned)ESP.getFreePsram());
        return false;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp;
    lv_disp_drv_init(&disp);
    disp.hor_res  = SCREEN_W;
    disp.ver_res  = SCREEN_H;
    disp.flush_cb = flush_cb;
    disp.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp);

    Serial.printf("display ready: %dx%d, psram_free=%u\n",
                  SCREEN_W, SCREEN_H, (unsigned)ESP.getFreePsram());
    return true;
}
