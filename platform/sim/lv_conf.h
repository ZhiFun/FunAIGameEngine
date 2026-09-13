/**
 * @file lv_conf.h
 * 模拟器(Windows)用的最小 LVGL 8.3 配置。其余选项全部走 lv_conf_internal.h 的默认值。
 * 分辨率与键盘实机一致(NV3007 428x142 横屏), 这样模拟器里验证过的游戏能直接上机。
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH       16
#define LV_COLOR_16_SWAP     0          // RGB565 直接映射(与引擎 Color 一致)

#define LV_HOR_RES_MAX       428
#define LV_VER_RES_MAX       142
#define LV_DPI_DEF           130
#define LV_DISP_DEF_REFR_PERIOD 16

#define LV_MEM_CUSTOM        0
#define LV_MEM_SIZE          (128U * 1024U)

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
