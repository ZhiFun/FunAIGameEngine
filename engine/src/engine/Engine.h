#pragma once
#include "Display.h"
#include "Input.h"
#include "Audio.h"
#include "AssetStore.h"
#include "Game.h"

namespace engine {

// 引擎门面: 游戏通过它访问显示/输入/音频/素材。
// 由平台 main 创建并驱动 frame()。
class Engine {
public:
    Display    display;
    AssetStore assets;
    Audio      audio;
    PadState   input;
    bool       running = true;

    void quit() { running = false; }

    bool load_assets(const char* dir) { return assets.init(dir); }
    const Image* img(const char* name) const { return assets.image(name); }
    const Clip*  snd(const char* name) const { return assets.clip(name); }

    // 平台主循环每帧调用: 先 update 后 render, 并提交画面 + 清掉本帧边沿。
    void frame(Game& game, float dt);
};

} // namespace engine
