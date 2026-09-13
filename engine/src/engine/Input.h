#pragma once
#include <stdint.h>

namespace engine {

// 标准手柄按键(与 docs/INPUT.md 一致)
enum class Button : uint8_t {
    Up = 0, Down, Left, Right,
    A, B, C, D,
    Start, Select,
    L, R, X, Y,
    Count
};
constexpr int kButtonCount = (int)Button::Count;

// 一帧的手柄状态。pressed/released 是"边沿", 只在产生该边沿的那一帧为真。
struct PadState {
    uint16_t held     = 0;   // 当前按住
    uint16_t pressed  = 0;   // 本帧上升沿
    uint16_t released = 0;   // 本帧下降沿
};

inline uint16_t bit_of(Button b) { return (uint16_t)(1u << (uint8_t)b); }

inline bool is_held(const PadState& s, Button b)     { return (s.held & bit_of(b)) != 0; }
inline bool is_pressed(const PadState& s, Button b)  { return (s.pressed & bit_of(b)) != 0; }
inline bool is_released(const PadState& s, Button b) { return (s.released & bit_of(b)) != 0; }

} // namespace engine
