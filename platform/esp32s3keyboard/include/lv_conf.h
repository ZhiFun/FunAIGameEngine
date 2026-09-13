/**
 * @file lv_conf.h
 * 本硬件实例(FunModularKeyboard)的 LVGL 8.3 配置。
 * 取值对齐键盘工程 include/lv_conf_local.h, 保证同一块屏上表现一致:
 *   - 16bit RGB565, 不交换字节(TFT_eSPI pushColors 时再 swap)
 *   - LVGL 内存池 256KB 并放到 PSRAM (内部 SRAM 只有 ~85KB 可用, 很紧张)
 *   - 刷新周期 16ms, 避免全屏动画时抖动
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH       16
#define LV_COLOR_16_SWAP     0

#define LV_HOR_RES_MAX       428
#define LV_VER_RES_MAX       142
#define LV_DPI_DEF           130
#define LV_DISP_DEF_REFR_PERIOD 16

// ---- 内存池: 从 PSRAM 拿, 避免顶满静态 64KB .bss ----
#include <esp_heap_caps.h>
#undef LV_MEM_SIZE
#define LV_MEM_SIZE (256U * 1024U)
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_USE_LOG           0
#define LV_USE_ASSERT_NULL   0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_PERF_MONITOR  0
#define LV_USE_MEM_MONITOR   0

#define LV_USE_DEMO_WIDGETS             0
#define LV_USE_DEMO_BENCHMARK           0
#define LV_USE_DEMO_STRESS              0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER  0
#define LV_USE_DEMO_MUSIC               0

#define LV_FONT_MONTSERRAT_14           1
#define LV_FONT_DEFAULT                 &lv_font_montserrat_14

#endif /* LV_CONF_H */
