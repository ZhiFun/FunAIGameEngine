#pragma once
// ============================================================================
// 键盘固件侧的引擎运行库(FunModularKeyboard 等宿主工程直接移植这个目录)
//
// 职责: 从 SPIFFS 读 .gbn 游戏文件 -> 建立 LVGL canvas -> 用 VM 逐帧跑脚本。
// 宿主只需要提供两件事:
//   * read_pad: 把机身按键读成标准手柄位图
//   * audio   : 音频后端(可以直接用键盘已有的 game_audio 混音器)
//
// 集成步骤见同目录 README.md。
//
// ⚠ 线程约定: tick() 必须由**已经持有 LVGL 的那个任务**调用(键盘是 DisplayTask),
//   否则会和宿主的 LVGL 操作打架。
// ============================================================================
#include "engine/Engine.h"
#include "engine/ScriptGame.h"

namespace game_runtime {

struct Config {
    // 返回 engine::Button 位图(bit i = Button i 当前按住)。必须提供。
    uint16_t (*read_pad)() = nullptr;

    // 音频输出后端。键盘已有混音器就传进来; nullptr = 静音。
    engine::AudioBackend* audio = nullptr;

    // 每帧开始回调(可选)。返回 false 表示宿主请求退出游戏(例如按了返回键)。
    bool (*on_frame)() = nullptr;

    int fps = 60;                 // 逻辑帧率上限
};

// 从 SPIFFS 载入 .gbn 并开始游戏。成功后 tick() 才会真正跑帧。
bool start(const Config& cfg, const char* gbn_path);

// 结束游戏并释放 canvas / 素材缓存。
void stop();

bool active();

// 宿主循环里调用(见上面的线程约定)。内部按 cfg.fps 限速。
void tick();

} // namespace game_runtime
