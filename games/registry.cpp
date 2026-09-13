#include "registry.h"
#include <cstring>

namespace games {

engine::Game* make_skyraider();   // 见 games/skyraider/game.h
engine::Game* make_fighter();     // 见 games/fighter/game.h
engine::Game* make_keychase();    // 见 games/keychase/game.h

namespace {

// 注册表: 新增游戏在这里加一条, 模拟器的下拉框会自动多一项。
// 第三列是探针贴图: 卡带里一定有的一张图, 用来判断"素材真的加载上了没有"。
const games::GameInfo kGames[] = {
    {"skyraider", "SKY RAIDER · 横版飞行射击",         "ship_f0"},
    {"fighter",   "KEY FIGHTER · 格斗 (1P vs CPU)",     "keyspr_idle0"},
    {"keychase",  "KEY CHASE · 键帽迷宫追逐",           "muncher_r0"},
    // 脚本卡带(游戏逻辑在 .gbn 字节码里, 没有任何原生 C++ 代码)。
    // 列在这里只是为了模拟器下拉框能选到; 固件那边不需要注册 —— 它直接扫 data/*.gbn。
    // probe = nullptr: 素材表由脚本自己决定, 未必有"一定有"的贴图。
    {"starfall",   "STARFALL · 接星星 (脚本卡带)",       nullptr},
    {"screentest", "SCREEN TEST · 脚本卡带冒烟测试",     nullptr},
    // VM 指令集冒烟测试(浮点/数组/位运算/字符串数组/翻转), 移植游戏前先跑它
    {"vmtest",     "VM TEST · 脚本指令冒烟测试",         nullptr},
};

} // namespace

engine::Game* create_game(const char* name) {
    if (name == nullptr) return nullptr;
    if (strcmp(name, "skyraider") == 0) return make_skyraider();
    if (strcmp(name, "fighter") == 0)   return make_fighter();
    if (strcmp(name, "keychase") == 0)  return make_keychase();
    return nullptr;
}

const games::GameInfo* game_list(int* count) {
    if (count != nullptr) *count = (int)(sizeof(kGames) / sizeof(kGames[0]));
    return kGames;
}

const char* game_probe_asset(const char* id) {
    if (id == nullptr) return nullptr;
    for (const GameInfo& g : kGames) {
        if (strcmp(g.id, id) == 0) return g.probe;
    }
    return nullptr;
}

bool cart_has_script(const engine::GamePackage* pkg) {
    return pkg != nullptr && pkg->loaded() && pkg->has_code();
}

engine::Game* create_from_cart(const engine::GamePackage* pkg, const char* id) {
    if (cart_has_script(pkg)) {
        auto* sg = new engine::ScriptGame();
        if (sg->init(pkg, (id != nullptr) ? id : "script")) return sg;
        delete sg;
        return nullptr;
    }
    return create_game(id);
}

const char* default_game_name() { return "skyraider"; }

} // namespace games
