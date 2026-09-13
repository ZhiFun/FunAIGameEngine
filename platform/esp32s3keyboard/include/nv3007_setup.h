#pragma once
// ============================================================================
// TFT_eSPI 硬件配置 —— 等价于 TFT_eSPI 库内 User_Setups/Setup350_NV3007.h
//
// 由 platformio.ini 的 `-include nv3007_setup.h` 预包含, 配合
// `-DUSER_SETUP_LOADED=1` 使用, 因此**不需要修改 TFT_eSPI 库文件**。
//
// 屏幕: NV3007, 面板 428x142(横屏), 带 12 像素 CGRAM 偏移。
// ⚠ 如果你换了屏或改了接线, 只需要改这个文件。
// ============================================================================

// NV3007 驱动定义(库内头文件, 提供命令码等)
#include <TFT_Drivers/NV3007_Defines.h>

#define TFT_DRIVER 0x3007
#define NV3007_DRIVER
#define CGRAM_OFFSET
#define TFT_RGB_ORDER TFT_RGB

// 面板在"竖屏"方向下的尺寸, 配合 setRotation(1) 得到 428x142 横屏
#define TFT_WIDTH  142
#define TFT_HEIGHT 428
#define TFT_PX_OFFSET_X 12
#define TFT_PX_OFFSET_Y 12

// ---- 背光: GPIO37, 低电平点亮 ----
#define TFT_BL 37
#define TFT_BACKLIGHT_ON LOW

// ---- SPI 引脚 ----
#define TFT_CS   40
#define TFT_DC   45
#define TFT_RST  46
#define TFT_MOSI 42
#define TFT_SCLK 41

#define SPI_FREQUENCY 60000000
#define USE_DMA       1

// 库自带字体(游戏自己画字, 这里保留默认以免库报警告)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
