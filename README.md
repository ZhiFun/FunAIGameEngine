# FunAIGameEngine

**当前版本：v1.0.1** —— 版本号唯一来源是 `engine/include/engine/Version.h`；改版本时同步
`lib/FunAIGameEngine/library.json` 的 `"version"` 与根 `CMakeLists.txt` 的 `project(... VERSION ...)`。

一个「用 AI 制作小游戏」的轻量 2D 游戏引擎：

- **一套 API，两处运行**：Windows 模拟器（Qt + LVGL + Qt Multimedia）里验证玩法；验证通过后一键导出 ESP32-S3 固件。
- **默认 LVGL 显示**：引擎用 LVGL 的显示驱动 + 全屏 `lv_canvas` 做像素渲染，游戏也可以直接使用 LVGL 控件。
- **标准化输入**：上/下/左/右 + A/B/C/D + Start/Select + L/R + X/Y，模拟器与硬件同一套语义（见 `docs/INPUT.md`）。
- **内置素材体系**：角色 / 背景 / 特效 / 音效 / BGM，统一命名与打包（见 `docs/ASSETS.md`）。
- **AI 是"程序员"**：引擎提供一份稳定的 SDK + 模板 + 命令行工具，AI 照着 `docs/AI_GAMEDEV_GUIDE.md` 写每个游戏的代码（见下文"流程"）。
- **架构总览**：整体分层、卡带格式、端到端用法、已知陷阱 → [`docs/ENGINE_ARCHITECTURE.md`](docs/ENGINE_ARCHITECTURE.md)。

## 目录结构

```
FunAIGameEngine/
├── engine/                 # 平台无关的引擎核心(纯 C++17, 只依赖 LVGL)
│   ├── include/engine/     #   Engine / Game / Display / Input / Audio / AssetStore ...
│   └── src/
├── games/                  # 原生 C++ 游戏 + 注册表(键盘固件不链接它们, 现留作规格参考)
│   ├── registry.{h,cpp}    #   create_game()/create_from_cart()/cart_has_script()
│   ├── fighter/            #   规格参考: 格斗(已脚本化)
│   ├── keychase/           #   规格参考: 吃豆人(已脚本化)
│   └── skyraider/          #   规格参考: 横版射击(已脚本化)
├── platform/
│   ├── sim/                # Windows 模拟器: Qt Widgets 窗口 + LVGL 驱动 + QAudioSink
│   └── esp32s3/            # ESP32-S3 导出模板(PlatformIO): TFT_eSPI + I2S 音频
├── assets-src/             # 原始素材(PNG / WAV), 按游戏分目录
├── assets-packed/          # 打包后的 .img/.snd(生成物, 运行时读取)
├── games-src/              # 【脚本游戏】<name>/game.gs + 素材
├── games-bin/              # 【脚本游戏】编译产物 <name>.gbn(可加载的游戏文件)
├── runtime/
│   └── keyboard/           # 键盘固件侧的运行库(移植这个目录即可打开 bin 玩)
├── tools/
│   ├── engine.py           # CLI: new / pack / assets / build / run / export
│   ├── gs_compiler.py      # 游戏脚本 -> 字节码
│   ├── build_game.py       # 脚本+素材 -> 单个 .gbn
│   ├── pack_assets.py      # PNG+WAV -> .img+.snd(纯标准库, 无依赖)
│   └── gen_sample_assets.py# 生成示例素材
└── docs/                   # 架构 / API / 输入 / 素材 / 脚本 / AI 写游戏指南
```

## 快速开始

前置：CMake ≥ 3.16、一个 C++17 编译器、Qt 6（本机 `C:\Qt\6.9.3\mingw_64`，与 FunKeyboardTools 同款，需含 Multimedia 组件）。
LVGL 8.3.11 已本地化到 `third_party/lvgl`，构建不需要联网。

```powershell
# 1) 生成并打包素材(纯标准库, 首次跑一次即可)
py -3 tools\gen_sample_assets.py
py -3 tools\gen_common_ui_assets.py     # 通用 UI(控件/HUD/覆盖层)
py -3 tools\gen_skyraider_ui_assets.py  # skyraider 座舱 HUD
py -3 tools\engine.py assets

# 2) 配置 + 编译模拟器(推荐直接用 Qt Creator 打开 CMakeLists.txt 选 MinGW 套件;
#    命令行如下, Qt 路径按本机改)
cmake -S . -B build -G "MinGW Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64 ^
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe ^
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build

# 3) 运行示例(默认 skyraider)
build\fun_sim.exe skyraider
```

> 也可以全走 CLI：`py -3 tools\engine.py configure --qt-dir C:/Qt/6.9.3/mingw_64`
> → `build` → `run skyraider`，它内部会先打包素材并自动定位 `fun_sim.exe`。

运行后看到 428×142 窗口（与键盘实机 NV3007 同分辨率），`方向键`移动、`A` 开火、`B` 技能、`Esc` 退出。

## 脚本游戏：游戏是一个可加载的 bin 文件（推荐）

游戏逻辑写在 `games-src/<name>/game.gs`，编译打包成**单个 `.gbn` 文件**（字节码 + 内嵌素材）。
键盘固件只移植一次引擎运行库，之后**换游戏只需换 bin 文件，不用重烧固件**。

```powershell
py -3 tools\engine.py pack starfall                  # -> games-bin/starfall.gbn
py -3 tools\engine.py run games-bin\starfall.gbn    # 模拟器直接跑 bin
```

游戏目录：

```
games-src/starfall/
  game.gs     脚本(语法见 docs/SCRIPT.md)
  star.png    素材(会打包成 star.img)
  coin.wav    音效(会打包成 coin.snd)
```

移植到键盘固件：见 `runtime/keyboard/README.md`（把 `data/*.gbn` 烧进 SPIFFS，
在宿主里调 `game_runtime::start/tick/stop`，约 20 行胶水代码）。

## 流程：做一个游戏

**推荐路线 —— 脚本卡带**（换游戏不用重烧固件，迭代也是秒级的）：

1. 建 `games-src/mygame/`，写 `game.gs`，素材（`.png`/`.wav`，或直接放现成的 `.img`/`.snd`）放旁边。
2. `py -3 tools\build_game.py mygame` —— 编译 + 打包成 `games-bin/mygame.gbn`。
3. `py -3 tools\gs_run.py games-src\mygame\game.gs --frames 600 --pad right,a --globals score`
   —— 字节码的 Python 参考实现 + 软件帧缓冲，秒级看逻辑对不对。
4. `py -3 tools\sim_shot.py tools\_preview\sim_mygame.png mygame --zoom 2` —— 真引擎渲染截图。
5. 拷 `mygame.gbn` 进键盘 `data/`，在货架上加一行条目，`upload` + `uploadfs`。

完整版见 [`docs/ENGINE_ARCHITECTURE.md`](docs/ENGINE_ARCHITECTURE.md) 的 §4。

**备选路线 —— 原生 C++ 游戏**（需要 VM 表达不了的东西时；代价是加游戏要重烧固件）：

1. `py -3 tools\engine.py new mygame` —— 从模板生成 `games/mygame/`。
2. AI 只写一个 `Game` 子类（`on_start/on_update/on_render`），调用 `engine.h` 里的 API（画图/输入/音频/素材），素材放进 `assets-src/mygame/`。
3. `py -3 tools\engine.py assets` —— 打包素材。
4. 模拟器里验证玩法。
5. `py -3 tools\engine.py export mygame` —— 生成 `out/mygame-esp32s3/`（PlatformIO 工程 + 已打包素材），
   `pio run -t upload` 即得到硬件固件。

详细契约见 `docs/AI_GAMEDEV_GUIDE.md`。

## 目前状态

- [x] 引擎核心：Display(全屏 canvas + 像素/图形/文字/贴图，支持水平翻转)、Input(标准手柄)、
      AudioMixer(音乐+音效同时混，按 clip 自带采样率取样)、AssetStore(.img/.snd)。
- [x] **字节码 VM + `.gbn` 卡带**：游戏是「一个可加载的文件」，不再是「一整套固件」。
      卡带 = 字节码 + 全部素材，零拷贝解析，直接喂给 `AssetStore`。
- [x] 脚本语言 v2：浮点 / 数组 / 字符串数组 / 函数 / 水平翻转 / 位运算（见 `docs/SCRIPT.md`）。
- [x] Windows 模拟器：Qt 窗口 + LVGL 显示驱动 + QAudioSink 音频 + 启动器（可选游戏）。
- [x] 离线工具链：参考 VM（`gs_run.py`）、引擎截图（`sim_shot.py`）、编译 traceback（`gs_trace.py`）、
      素材打包/策略强制（`pack_assets.py` / `build_game.py` / `sndlib.py`）。
- [x] 素材打包器（PNG 解码 / WAV 重采样，纯标准库）+ 示例素材生成器。
- [x] ESP32-S3 导出模板（PlatformIO + TFT_eSPI + I2S 混音），导出后按板子改一下引脚即可。
- [x] **三个完整脚本游戏**：`keychase`（吃豆人）、`skyraider`（横版射击）、`fighter`（1v1 格斗）。
      键盘固件里**不再链接任何原生游戏实现**，只负责把 `data/*.gbn` 读进 PSRAM 并启动 VM。

> 硬件端需要按你的板子接线微调 `platform/esp32s3/src/esp_display.cpp`（SPI 引脚）与
> `esp_audio.cpp`（I2S 引脚），其余逻辑与模拟器完全一致。
>
> ⚠ 卡带住在 SPIFFS 分区（总共 917,504 字节，可用约 844 KB），所以有两条**由构建强制**的
> 约束：音频采样率 ≤ 11025 Hz、只打包脚本真正引用过的素材。别绕过它们，见
> `docs/ENGINE_ARCHITECTURE.md` §5.7。
