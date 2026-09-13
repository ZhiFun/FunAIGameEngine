# 素材规范

## 目录与命名

原始素材放在 `assets-src/`，按游戏分目录；`common/` 放跨游戏共用的：

```
assets-src/
├── common/            # 所有游戏可用
│   ├── bg_stars.png
│   └── click.wav
└── skyraider/         # 某个游戏专用
    ├── player.png
    ├── enemy.png
    ├── bullet.png
    ├── sfx_shoot.wav
    └── bgm.wav
```

`tools/engine.py assets` 会递归打包成 `assets-packed/`，运行时的名字就是相对路径去掉扩展名：
例如 `assets-src/skyraider/player.png` → `assets-packed/skyraider/player.img`，
游戏里用 `e.img("skyraider/player")`、`e.snd("skyraider/sfx_shoot")` 读取。

## 图片 (.png)

- 任意尺寸，打包时转成 RGB565。
- **透明色约定**：用纯品红 `#FF00FF` 作为透明（chroma-key），打包器会保留该值，运行时
  `display.draw_image_alpha(..., COLOR_MAGENTA)` 跳过它。简单素材直接用不透明即可。
- 建议：角色/背景用不透明 PNG；子弹/粒子用品红透明。

## 通用 UI 素材

运行 `py -3 tools\gen_common_ui_assets.py` 生成跨游戏可复用的原创 8-bit 主机风格 UI
素材到 `assets-src/common/`；生成后运行 `py -3 tools\engine.py assets` 打包。预览图输出到
`tools/_preview/common_ui.png`。设计采用自建有限色板、硬轮廓、倒角与台阶高光，不使用任何
现有 FC 游戏的角色、文字、地图、界面布局或标识。所有图标以品红为透明色，适合
`display.draw_image_alpha(..., COLOR_MAGENTA)`。

| 分类 | 素材名 | 尺寸 | 用途 |
|---|---|---:|---|
| 容器 | `common/panel`、`common/dialog` | 64x48、160x72 | 面板与对话框背景 |
| 按钮 | `common/button_normal`、`button_pressed`、`button_disabled` | 96x32 | 三种交互状态 |
| 输入 | `common/control_dpad`、`control_a`、`control_b` | 72x72、32x32 | 虚拟手柄提示 |
| 导航 | `common/arrow_left/right/up/down` | 20x20 | 翻页、列表与方向提示 |
| 命令 | `common/icon_pause/play/stop/settings` | 32x32 | 常用操作图标 |
| 状态 | `common/status_heart/coin/gem` | 24x22、24x24 | 生命、货币与收集物 |
| 进度 | `common/bar_health`、`common/bar_energy` | 96x12 | HUD 状态条 |
| 覆盖层 | `common/overlay_crosshair/cursor/select/spinner` | 32x32、18x24、48x48、32x32 | 瞄准、指针、选择和加载状态 |

## SKY RAIDER 专用 UI

运行 `py -3 tools\gen_skyraider_ui_assets.py` 生成默认示例游戏的原创飞船座舱 HUD 素材，
再运行 `py -3 tools\engine.py assets`。`skyraider/game.cpp` 会自动加载并使用：
`skyraider/ui_top_frame`、`ui_score_plate`、`ui_lives_plate`、`ui_prompt_plate`、
`ui_life_pip`、`ui_shield_bar` 与 `ui_target_lock`。预览图位于
`tools/_preview/skyraider_ui.png`。

## 音频 (.wav)

- 任意采样率/位深/声道，打包器统一重采样为 **22050 Hz 单声道 16bit**（引擎混音器的固定采样率）。
- `sfx_*` 前缀 = 音效（一次性播放），`bgm*` 前缀 = 背景音乐（循环播放，`audio.play_bgm`）。

## 运行时格式（assets-packed/）

打包器输出两个极其简单的二进制格式，运行时无需任何解码库：

- `.img`：`"IMG1"` + uint16 w + uint16 h + w*h*2 字节 RGB565（小端）。
- `.snd`：`"SND1"` + uint32 sampleRate + uint32 frameCount + frameCount*2 字节 int16 单声道。

> 需要导出到硬件时，`tools/engine.py export` 会把 `assets-packed/` 原样放进
> PlatformIO 工程的 `data/`（SPIFFS），`pio run -t uploadfs` 烧进去。

## 素材怎么进固件：卡带 ROM 模型（推荐做法）

**核心问题**：把素材放在文件系统里（模拟器的磁盘目录、ESP32 的 SPIFFS），故障只在
运行期暴露 —— 目录没上传、`uploadfs` 没跑、文件被裁掉、路径拼错，结果就是
**游戏能跑但图片/音乐全没了**。本项目已经踩过一次：`platform/esp32s3keyboard/data/`
根本不存在，于是所有 `.img` 精灵静默消失（字符画的怪物还在，看起来就像"部分素材没了"）。

街机、FC、GBA 从来不这么干 —— 它们的素材是**卡带 ROM 的一部分**：

| 平台 | 素材存放 | 访问方式 |
|---|---|---|
| FC/NES | PRG ROM + CHR ROM（图案表 8x8、2bpp、4 色调色板） | PPU 直接寻址；精灵经 OAM（64 个，每扫描线最多 8 个）合成，无帧缓冲 |
| GBA | 32MB 地址空间的 ROM（从 `0x08000000` 起） | CPU/DMA 直接读；素材常压缩，解压例程在 BIOS ROM 里 |
| SNES | 卡带 ROM + 可扩展显存 | DMA 搬运到 VRAM / CGRAM / OAM |

共同点：**没有文件系统、没有文件名、没有"加载失败"**。游戏和素材是同一个镜像，
版本天然一致，刷一次就全带上了。

本引擎用同样的做法 —— **`.gbn` 就是卡带**：先打成素材卡带，再把卡带内嵌进固件
（两边是同一份字节）：

```bash
# 1) 把 assets-packed/<游戏>/ + common/ 打成素材卡带
py -3 tools\build_game.py --cart skyraider          # -> games-bin/skyraider.gbn

# 2) 把卡带内嵌进固件（生成 engine/src/rom_assets_data.h）
py -3 tools\embed_assets.py --src games-bin\skyraider.gbn
# 或在 CMake 里（需要系统能找到 python）：
cmake --build <build-dir> --target embed-assets
```

卡带容器格式与脚本游戏**完全一致**（见 `docs/SCRIPT.md`），区别只是没有入口点、
`codeSize=1`（一条 `HALT` 占位 —— 解析器要求非 0，用来挡住被截断的文件）。
素材名是打平后的 basename，所以 `AssetStore` 不关心游戏是 C++ 写的还是脚本写的。

运行时的素材查找顺序（模拟器）：

1. 命令行参数给了 `.gbn` → 那是脚本游戏卡带，同时也是素材来源
2. `games-bin/<游戏名>.gbn` → 素材卡带（**改卡带不用重新编译**）
3. 编译进 exe 的内嵌卡带内容
4. `assets-packed/` 目录（开发期兜底；`FUNSIM_ASSETS=disk` 可强制走这条）

ESP32 上用 2/3 —— 刷一次固件素材就齐了，`data/` + `uploadfs` 不再是必需的。

生成的头文件把每个素材变成一段 `const` 字节数组（进 `.rodata`，在 ESP32 上就是 flash
里的常量），外加一张 `{ "skyraider/player.img" -> (数据, 长度) }` 查表。运行期由
`engine::RomFileSystem` 按名字查表返回，**完全不碰文件系统**。平台侧（模拟器与
ESP32 都已接好）：

```cpp
if (engine::rom_fs().available()) {
    engine.assets.set_fs(&engine::rom_fs());
    engine.assets.init("");
} else {
    // 内嵌表为空 -> 退回磁盘 / SPIFFS（保留给开发期热改素材）
}
```

- **模拟器**：默认走内嵌表；想边改素材边看效果，设 `FUNSIM_ASSETS=disk` 强制走
  `assets-packed/` 目录。
- **ESP32**：默认走内嵌表，刷一次固件素材就齐了；`data/` + `uploadfs` 变成可选的
  开发期手段，不再是必需的。

代价：素材会进固件镜像（本项目 40 个素材约 371 KB，4MB flash 完全放得下），改素材
要重新编译。这是卡带模型固有的取舍 —— 换来的是一整类运行期故障彻底消失。

> 更细粒度的做法（按游戏分卡带）已经在 `.gbn` 游戏包上实现：`.gbn` 把某个游戏的
> 字节码和素材打成一个文件，加载后同样通过 `FileSystem` 接口喂给 `AssetStore`
> （见 `docs/SCRIPT.md`）。C++ 游戏要按游戏拆素材时，可以只对
> `assets-packed/<game>/` 生成一张表。

