# 标准输入规范

引擎把「实体按键」抽象成一个统一的手柄，模拟器与硬件完全同语义。
游戏只认 `Button` 枚举，不关心底层是键盘还是 GPIO。

## 按键枚举（engine::Button）

| 枚举 | 含义 | 参考主流游戏机 |
|---|---|---|
| `Up/Down/Left/Right` | 十字方向键 | D-pad |
| `A / B / C / D` | 四个主按键 | SEGA / NeoGeo 风格 ABCD |
| `Start / Select` | 开始 / 选择 | 标配 |
| `L / R` | 左右肩键 | SNES/PS 风格 |
| `X / Y` | 两个扩展按键 | SNES 风格 |

## 手柄状态（engine::PadState）

```cpp
struct PadState {
    uint16_t held     = 0;   // 当前按住
    uint16_t pressed  = 0;   // 本帧刚按下(上升沿, 一帧有效)
    uint16_t released = 0;   // 本帧刚松开(下降沿, 一帧有效)
};
bool engine::is_held(const PadState&, Button);
bool engine::is_pressed(const PadState&, Button);
bool engine::is_released(const PadState&, Button);
```

`pressed/released` 是"边沿"，只在一个逻辑帧内为真，适合做"按一下跳一下"这种触发；
`held` 是持续状态，适合"按住加速"。

## Windows 模拟器键盘映射

| 手柄 | 键盘 |
|---|---|
| Up/Down/Left/Right | 方向键 |
| A / B / C / D | 字母键 A / B / C / D |
| X / Y | 字母键 X / Y |
| L / R | Q / R |
| Start | Enter |
| Select | Space |
| 退出模拟器 | Esc |

> 字母键与手柄键同名，按下 `A` 就是手柄的 A。
>
> 窗口必须**获得焦点**才能收到按键（点一下窗口即可）。如果按下无反应，先看标题栏
> 是不是处于非激活状态；想确认事件是否到达，可用 `FUNSIM_KEYLOG=1` 启动，
> 每次按键会往 stderr 打一行 `btn=` / `held=` 日志。

## 硬件映射

- **键盘硬件实例（默认）**：`platform/esp32s3keyboard/src/esp_main.cpp` 顶部的
  `kRowPins[] / kColPins[]`（5×4 矩阵）与 `kKeyMap[]`（矩阵位置 → Button）。
- **通用 ESP32-S3 模板**：`platform/esp32s3/src/esp_main.cpp` 的 `kKeys[]`（GPIO → Button）。

按你的实际接线改这几个表即可。
