# AI 写游戏指南（给 AI 的契约）

本引擎的定位：**AI 是程序员**。AI 拿到这份契约 + `docs/ENGINE_API.md` + `docs/INPUT.md` +
`docs/ASSETS.md`，就可以独立产出一个可运行的 `Game` 子类。示例见 `games/skyraider/`。

## 一个游戏 = 三样东西

1. **一个 `Game` 子类**（`games/<name>/game.cpp`），实现 `name/on_start/on_update/on_render`。
2. **素材**放进 `assets-src/<name>/`（图片 `.png`、音频 `.wav`）。
3. **注册**：在 `games/registry.cpp` 加一行 `{ "mygame", &make_mygame }`。

## 游戏骨架模板

```cpp
#include "engine/Engine.h"
using namespace engine;

namespace {

class MyGame : public Game {
    float x = 0, y = 0;
public:
    const char* name() const override { return "mygame"; }

    void on_start(Engine& e) override {
        e.audio.play_bgm(e.snd("mygame/bgm"), 0.8f);
    }

    void on_update(Engine& e, float dt) override {
        const float spd = 120.0f * dt;
        if (is_held(e.input, Button::Left))  x -= spd;
        if (is_held(e.input, Button::Right)) x += spd;
        if (is_held(e.input, Button::Up))    y -= spd;
        if (is_held(e.input, Button::Down))  y += spd;
        if (is_pressed(e.input, Button::A)) {
            e.audio.play_sfx(e.snd("mygame/sfx_click"));
        }
    }

    void on_render(Engine& e) override {
        e.display.clear(rgb565(8, 8, 24));
        e.display.text(8, 8, "MY GAME", rgb565(255,255,255), 2);
        if (const Image* p = e.img("mygame/player")) {
            e.display.draw_image_alpha((int)x, (int)y, *p, rgb565(255,0,255));
        }
    }
};

} // namespace

engine::Game* make_mygame() { return new MyGame(); }
```

## 必须遵守的规则

1. **只用 `dt` 步进**，不要写死 `delay()` / 忙等 —— 逻辑帧率可能变化。
2. **每帧都要画完整画面**（先 `display.clear()` 再画），不要依赖上一帧残留。
3. **素材名**用打包名（目录/文件名，无扩展名），且在 `on_start` 里判空再画：
   `const Image* p = e.img("x"); if (p) ...`。
4. **音效可叠加、BGM 唯一**：`play_bgm` 会替换当前 BGM；`play_sfx` 与 BGM 同时出声。
5. **不要在 `on_render` 里改逻辑状态**，也不要在 `on_update` 里绘制（保持可预测）。
6. 需要多关卡/多场景：在 `Game` 内部用状态机（enum Phase），不要依赖外部。
7. 平台无关：不要在游戏里 include `windows.h` / Arduino / SPIFFS / GPIO 等。

## 产出验收标准

- [ ] `py -3 tools\engine.py assets` 无错误。
- [ ] 模拟器 `fun_sim.exe <name>` 能跑、操作跟 `docs/INPUT.md` 一致。
- [ ] `py -3 tools\engine.py export <name>` 能生成 `out/<name>-esp32s3/`，`pio run` 编译通过。
- [ ] 游戏在模拟器和硬件上行为一致（同一个 `Game` 源码）。

## 给 AI 的推荐 Prompt（可直接复制）

> 你是 FunAIGameEngine 的开发者。引擎 C++17 + LVGL 显示，提供 `Engine` 门面：
> `display`（clear/fill_rect/draw_image_alpha/text…）、`input`（is_held/is_pressed，按键
> Up/Down/Left/Right/A/B/C/D/Start/Select/L/R/X/Y）、`audio`（play_bgm/play_sfx）、
> `assets`（img/snd）。请只实现一个 `Game` 子类，严格按 docs/ENGINE_API.md，
> 用 dt 步进、每帧清屏重画、素材判空。现在实现「<在这里描述玩法>」，素材名用
> `<game>/<asset>` 前缀。
