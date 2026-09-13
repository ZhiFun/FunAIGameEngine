# FunAIGameEngine（库本体）

**v1.0.1** —— 卡带式 2D 游戏引擎。这个目录就是一个**标准 Arduino 库**：
Arduino IDE / PlatformIO 都能直接吃进去，不需要 CMake，也不需要改任何源码路径。

> 仓库根目录是 CMake 工程（模拟器 + 导出模板）；**这个 `engine/` 目录是发布单位**。
> 两者的关系见根目录 `README.md` 和 `docs/ENGINE_ARCHITECTURE.md`。

---

## 1. 目录布局（为什么长这样）

```
FunAIGameEngine/            ← 库根（本目录）
├── library.properties      ← Arduino IDE 清单（名字/版本/依赖）
├── library.json            ← PlatformIO 清单
├── keywords.txt            ← IDE 语法高亮
├── src/
│   ├── FunAIGameEngine.h   ← 一把梭头文件: #include <FunAIGameEngine.h>
│   └── engine/
│       ├── Engine.h  Display.h  Game.h  Input.h  Types.h
│       ├── AssetStore.h  Audio.h  AudioMixer.h
│       ├── ScriptGame.h  RomFileSystem.h  Version.h
│       └── 对应的 .cpp
└── examples/
    └── lvgl_hello/         ← 最小可用示例（LVGL + 引擎 + 一个 C++ 小游戏）
```

两条**不能动**的规矩：

1. **头文件和 `.cpp` 必须在同一个目录（`src/engine/`）里。**
   引擎内部用的是同目录带引号的 include（`#include "AssetStore.h"`），
   一旦把 `.h` 挪回 `include/`，Arduino IDE 就找不到它们了 —— 它只会把 `src/` 加进搜索路径。
2. **外部一律写 `#include <engine/xxx.h>`**（或 `"engine/xxx.h"`）。
   因为库根把 `src/` 暴露为搜索路径，所以 `engine/` 这个前缀是稳定的。

## 2. 依赖

| 依赖 | 说明 |
|---|---|
| **LVGL 8.3.x** | **必需**。`Display` 就在全屏 `lv_canvas` 上画像素，所以没有 LVGL 编不过 |
| 目标平台 | `esp32`（`architectures=esp32`）。引擎核心与平台无关，但要有 LVGL + 一块够大的 RAM |

`library.properties` 里写了 `depends=lvgl`，Arduino IDE 2.x 会在装库时提示一并装 LVGL。

## 3. 安装

**Arduino IDE**

```
把整个 engine/ 目录拷成   <你的文档>/Arduino/libraries/FunAIGameEngine/
```
（目录名要和 `library.properties` 的 `name` 一致；或者把 `engine/` 打包成 zip 后用
「项目 → 加载库 → 添加 .ZIP 库」，注意 zip 里要有一个 `FunAIGameEngine/` 顶层目录。）

**PlatformIO**

```ini
; 直接用本地路径
lib_deps =
    file:///F:/New_Project/FunAIGameEngine/engine
    lvgl/lvgl@8.3.11
```
或者把 `engine/` 拷进工程的 `lib/FunAIGameEngine/`（PlatformIO 会自动把 `src/` 加进搜索路径，
**不再需要**以前那种 `-I$PROJECT_DIR/lib/FunAIGameEngine/src` 的补丁）。

装好之后：

```cpp
#include <FunAIGameEngine.h>
```

## 4. 宿主（你的板子）要提供的三样东西

引擎不认识你的屏、按键和功放，这三块要你接（完整 API 见 `docs/ENGINE_API.md`）：

| 要提供 | 接口 | 备注 |
|---|---|---|
| LVGL 显示驱动 | `Display::init()` + `Display::set_present_hook(fn, ctx)` | 引擎把脏矩形直接交给 hook 推屏；不设 hook 就走 LVGL 自己的刷新 |
| 手柄位掩码 | 直接写 `Engine::input.held / pressed / released` | 位定义见 `engine/Input.h` |
| 音频后端 | `engine::AudioBackend` → `Engine::audio.attach(&backend)` | 不想出声就别 attach |

## 5. 最小用法

```cpp
#include <FunAIGameEngine.h>

class Demo : public engine::Game {
public:
    const char* name() const override { return "demo"; }
    void on_start(engine::Engine& e) override { x_ = 40; }
    void on_update(engine::Engine& e, float dt) override {
        // e.input.held / pressed: 位掩码, 用 engine::bit_of(engine::Button::Left) 取位
        if (engine::is_pressed(e.input, engine::Button::Right)) x_ += 2;
    }
    void on_render(engine::Engine& e) override {
        e.display.fill_rect(0, 0, 428, 142, engine::rgb565(8, 10, 18));
        e.display.fill_rect(x_, 60, 16, 16, engine::rgb565(255, 120, 60));
    }
private:
    int x_ = 40;
};

engine::Engine  g_engine;
Demo            g_demo;

void setup() {
    // 1) 先把 LVGL 拉起来(display driver + flush_cb), 见 examples/
    // 2) 再交给引擎
    if (!g_engine.display.init()) return;
    g_engine.running = true;
    g_demo.on_start(g_engine);
}

void loop() {
    const float dt = 1.0f / 30.0f;
    g_demo.on_update(g_engine, dt);
    g_demo.on_render(g_engine);
    g_engine.display.present();
    delay(33);
}
```

跑**卡带**（`.gbn`，游戏逻辑在文件里）时不写 C++ 游戏类，改成：

1. 把 `.gbn` 读进一块常驻内存（几百 KB，要放 PSRAM）；
2. `engine::GamePackage::parse(buf, size)`（零拷贝）；
3. `e.assets.set_fs(&pkg); e.load_assets("/");`
4. `engine::ScriptGame` 跑 VM。

**完整可运行的参考实现**（含 LVGL 驱动、I2S 音频交接、切屏与退出清理）在键盘固件里：
`FunModularKeyboard/firmware/FunModularKeyboard/src/game/game_port.cpp`，
说明见同仓库 `docs/GAME_ENGINE_INTEGRATION.md`。

## 6. 版本号

唯一来源是 `src/engine/Version.h`。改版本时同步：

* `library.properties` 的 `version=`
* `library.json` 的 `"version"`
* 仓库根 `CMakeLists.txt` 的 `project(... VERSION ...)`
