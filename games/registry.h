#pragma once
#include "engine/Game.h"
#include "engine/ScriptGame.h"

namespace games {

// 按注册名创建游戏(注册表见 registry.cpp)。返回 nullptr 表示不存在。
engine::Game* create_game(const char* name);

// 未指定名字时运行的默认游戏
const char* default_game_name();

// 已注册的 C++ 游戏清单 —— 给模拟器/工具做下拉框用, 顺序就是显示顺序。
// 一个游戏 = 注册名 + `games-bin/<id>.gbn` 卡带。
struct GameInfo {
    const char* id;      // create_game() 用的名字
    const char* label;   // 显示名(UTF-8)
    const char* probe;   // 探针贴图名: 素材源里找不到它就说明卡带/素材没加载对
                         // (和固件 ui_*_launch() 里传的那张图是同一个)
};
const GameInfo* game_list(int* count);

// 取注册游戏的第一张"探针"贴图名; 没注册过返回 nullptr。
// 用途: 开跑之前确认素材源(卡带 / assets-packed / 内嵌表)里真的有这个游戏的图。
// 少了它, 游戏只会画出程序生成的背景, 人物/子弹全体消失 —— 看起来像渲染坏了,
// 实际是素材源选错了(模拟器从非工程目录启动时最常踩)。
const char* game_probe_asset(const char* id);

// ---------------------------------------------------------------------------
// 卡带驱动启动 —— **固件/模拟器只应该用这一个入口**
//
//   · 卡带里带字节码(GamePackage::has_code())-> 跑 VM(engine::ScriptGame),
//     游戏逻辑全在 .gbn 里, 固件不需要为它写过任何代码;
//   · 否则 -> 回落 create_game(id), 也就是同名的原生 C++ 游戏
//     (skyraider / fighter / keychase 走这条, 卡带对它们而言只是素材来源);
//   · 两者都不成立返回 nullptr。
//
// pkg 必须比返回的游戏活得久(ScriptGame 不拥有它, 只存指针)。
engine::Game* create_from_cart(const engine::GamePackage* pkg, const char* id);

// 卡带是不是"自带游戏逻辑"(脚本卡带)。
// 用给上层做判断: 脚本卡带不需要探针贴图(素材表可能就几张图, 甚至一张都没有)。
bool cart_has_script(const engine::GamePackage* pkg);

} // namespace games
