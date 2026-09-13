#pragma once
// ---------------------------------------------------------------------------
// FunAIGameEngine 版本号 —— **唯一来源**
//
// 改版本号时只改这里, 然后同步这两处(它们没法自动读宏):
//   · lib/FunAIGameEngine/library.json 的 "version"   （PlatformIO 库清单）
//   · CMakeLists.txt 的 project(... VERSION ...)      （模拟器构建）
// 固件进游戏时会串口打印 `FunAIGameEngine v...`, 方便确认设备上跑的是哪一版。
//
// 编号约定:
//   1.x.x   引擎能跑在键盘固件里的主线形态: 卡带(.gbn) + 字节码 VM
//   x.0.x   不兼容变更 —— 改了卡带格式 / 指令集 / 素材格式(老卡带要重打)
//   x.x.N   修 bug / 加游戏 / 加指令(卡带向后兼容)
// ---------------------------------------------------------------------------

#define FUN_ENGINE_VERSION_MAJOR 1
#define FUN_ENGINE_VERSION_MINOR 0
#define FUN_ENGINE_VERSION_PATCH 1

#define FUN_ENGINE_VERSION_STR "1.0.1"

namespace engine {

// 运行时取版本(打日志 / 显示用)。
// 刻意不用 C++17 的 `inline constexpr` 变量 —— 键盘固件的 `src/` 是按 C++11 编的,
// 这个头会被那边的 game_port.cpp 包含。constexpr 变量隐含 const, C++11 下是内部链接,
// 每个 TU 一份, 不会有 ODR 问题。
constexpr const char* kVersionString = FUN_ENGINE_VERSION_STR;
constexpr int kVersionMajor = FUN_ENGINE_VERSION_MAJOR;
constexpr int kVersionMinor = FUN_ENGINE_VERSION_MINOR;
constexpr int kVersionPatch = FUN_ENGINE_VERSION_PATCH;

} // namespace engine
