#pragma once
#include <stdint.h>

namespace engine {

// RGB565 颜色(与 LVGL 16bit 一致)
using Color = uint16_t;

struct Vec2i { int x = 0, y = 0; };
struct Rect  { int x = 0, y = 0, w = 0, h = 0; };

// 透明色约定(见 docs/ASSETS.md)
constexpr Color kColorMagenta = (Color)(((31) << 11) | (0 << 5) | (31)); // #F800F8 -> 565

inline constexpr Color rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (Color)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
}

} // namespace engine
