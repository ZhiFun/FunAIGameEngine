# 移植到键盘固件 (FunModularKeyboard)

这个目录把引擎的**运行库**搬到键盘固件里，让键盘能"打开游戏 bin 直接玩"。
游戏本体不进固件，只有引擎运行库进固件。

## 1. 拷文件

把下面这些复制进键盘工程（建议放 `src/engine/` 与 `src/game_runtime/`）：

```
engine/include/engine/     ->  工程 include 路径里(如 lib/engine/)
engine/src/Engine.cpp          (注意只拷这 6 个, 不要拷 Display 之外的平台代码)
engine/src/Display.cpp
engine/src/AudioMixer.cpp
engine/src/Audio.cpp
engine/src/AssetStore.cpp
engine/src/ScriptGame.cpp
runtime/keyboard/game_runtime.{h,cpp}
```

**不要**拷 `platform/` 下的任何东西——键盘已有自己的 TFT_eSPI + LVGL 显示，
运行库只在现有 LVGL 屏幕上建 canvas，**不会再注册显示驱动**。

依赖：键盘工程已有的 `lvgl@8.3.11` 就够（VM 是纯 C++17，不依赖 LVGL）。

## 2. 放游戏文件

把 `games-bin/*.gbn` 放进键盘工程的 `data/`（SPIFFS），然后 `pio run -t uploadfs`。
运行时路径就是 `/starfall.gbn`。

## 3. 接进来（约 20 行）

```cpp
#include "game_runtime.h"

// ---- 输入: 把机身矩阵读成标准手柄位图 ----
static uint16_t read_pad_for_game() {
    uint16_t held = 0;
    // 用你现成的矩阵扫描结果, 例如:
    //   if (key_is_down(KEY_LEFT))  held |= (1u << (uint8_t)engine::Button::Left);
    //   if (key_is_down(KEY_Z))     held |= (1u << (uint8_t)engine::Button::A);
    // 也可以复用 RotaryEncoder: 左旋 -> Left, 右旋 -> Right, 按下 -> A
    return held;
}

// ---- 每帧回调(可选): 这里判断"退出游戏" ----
static bool game_frame_hook() {
    return true;          // 返回 false 就退出游戏, 回到键盘 UI
}

// 进入游戏时:
void enter_game(const char* gbn_path) {
    game_audio::grant_output();            // 键盘已有的 I2S 交接握手(重要!)
    game_runtime::Config cfg;
    cfg.read_pad = read_pad_for_game;
    cfg.audio    = game_audio::backend();  // 或 nullptr = 静音
    cfg.on_frame = game_frame_hook;
    if (!game_runtime::start(cfg, gbn_path)) {
        game_audio::reclaim_output();
    }
}

// 在 DisplayTask(或任何已经持有 LVGL 的任务)的循环里:
void display_task_loop() {
    if (game_runtime::active()) {
        game_runtime::tick();              // 必须在持有 LVGL 的任务里调
    } else {
        // ... 键盘原有的 UI 刷新 ...
    }
}

// 退出游戏后:
void leave_game() {
    game_runtime::stop();
    game_audio::reclaim_output();
}
```

## 4. 两个坑（键盘工程已踩过）

1. **I2S 只能有一个主人**。键盘的 `Speaker` 库和游戏混音器抢同一个
   `I2S_NUM_1`(BCLK16/LRC39/DOUT38)，必须走 `grant_output()` /
   `reclaim_output()` 握手，**绝不能**在库正在写 I2S 时 uninstall 驱动。
2. **LVGL 不是线程安全的**。`tick()` 只能在持有 LVGL 的那个任务里调；
   不要在 MainTask 里直接调。

## 5. 内存

- `.gbn` 整包读进 **PSRAM**（运行库内部优先用 `heap_caps_malloc(SPIRAM)`）。
- 素材是**懒加载**的：只有脚本真正用到的图/音才会解到内存里，并缓存在 AssetStore。
- VM 每帧的栈是固定 256 个 int32(1KB)，不会动态分配。
