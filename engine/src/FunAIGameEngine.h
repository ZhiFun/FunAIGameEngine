#pragma once
// ---------------------------------------------------------------------------
// FunAIGameEngine —— 一把梭头文件。只 include 这一个就能拿到全部公开 API:
//
//   #include <FunAIGameEngine.h>
//
// 两种写游戏的方式(可以混用):
//   · C++  : 继承 engine::Game, 实现 on_start / on_update / on_render
//   · 脚本 : games-src/<name>/game.gs 用 tools/gs_compiler.py 编成 .gbn,
//            运行时 engine::ScriptGame 跑字节码 VM。**换游戏不用重烧固件**
//
// 宿主(平台侧)要提供三样东西, 见 docs/ENGINE_API.md:
//   · LVGL 显示驱动(引擎在全屏 lv_canvas 上画像素)
//   · 手柄位掩码(engine::Button 的位)
//   · engine::AudioBackend(不想出声可以不挂)
// ---------------------------------------------------------------------------

#include "engine/Version.h"      // 版本号(唯一来源)
#include "engine/Types.h"        // Color / Rect / Image / Clip
#include "engine/Input.h"        // Button 枚举 + 位运算
#include "engine/Game.h"         // 游戏基类
#include "engine/Display.h"      // 画布 / 像素 / 图形 / 文字 / 贴图
#include "engine/AssetStore.h"   // .img / .snd 按名字取, FileSystem 可换
#include "engine/Audio.h"        // 音频后端抽象
#include "engine/AudioMixer.h"   // 1 条 BGM + 6 声部, 按 clip 自带采样率取样
#include "engine/Engine.h"       // 门面: display / input / assets / audio
#include "engine/RomFileSystem.h"// 内嵌素材表(把素材编进固件的旧路线)
#include "engine/ScriptGame.h"   // GamePackage(.gbn) + 脚本 VM
