// 键盘固件侧的引擎运行库实现。依赖: Arduino(SPIFFS/millis) + LVGL(宿主机已初始化)。
#include "game_runtime.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "engine/ScriptGame.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace game_runtime {
namespace {

Config        g_cfg;
engine::Engine* g_engine = nullptr;
engine::GamePackage* g_pkg = nullptr;
engine::ScriptGame* g_game = nullptr;
uint8_t*      g_blob = nullptr;      // .gbn 整包(PSRAM 优先)
size_t        g_blob_size = 0;
uint32_t      g_last_ms = 0;
uint16_t      g_prev_held = 0;
bool          g_active = false;

// SPIFFS 适配: .gbn 直接放 SPIFFS 根目录
struct SpiffsFs : engine::FileSystem {
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        File f = SPIFFS.open(path.c_str(), "r");
        if (!f) return false;
        out.resize(f.size());
        if (!out.empty()) f.read(out.data(), out.size());
        f.close();
        return true;
    }
};
SpiffsFs g_fs;

void release_all() {
    if (g_engine != nullptr) {
        g_engine->display.deinit();
        g_engine->assets.clear();
    }
    delete g_game;  g_game = nullptr;
    delete g_pkg;   g_pkg = nullptr;
    delete g_engine; g_engine = nullptr;
    if (g_blob != nullptr) {
        free(g_blob);
        g_blob = nullptr;
    }
    g_blob_size = 0;
    g_prev_held = 0;
}

} // namespace

bool start(const Config& cfg, const char* gbn_path) {
    stop();

    if (cfg.read_pad == nullptr || gbn_path == nullptr) return false;

    File f = SPIFFS.open(gbn_path, "r");
    if (!f) {
        Serial.printf("[game_runtime] 找不到游戏文件: %s\r\n", gbn_path);
        return false;
    }
    const size_t size = f.size();
    if (size < 36) {
        f.close();
        Serial.printf("[game_runtime] 游戏文件太小: %u\r\n", (unsigned)size);
        return false;
    }
    // 整包放 PSRAM(内部 SRAM 很紧张)
    g_blob = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_blob == nullptr) g_blob = (uint8_t*)malloc(size);
    if (g_blob == nullptr) {
        f.close();
        Serial.printf("[game_runtime] 内存不足: %u 字节\r\n", (unsigned)size);
        return false;
    }
    const size_t got = f.read(g_blob, size);
    f.close();
    if (got != size) {
        Serial.printf("[game_runtime] 读取不完整: %u/%u\r\n", (unsigned)got, (unsigned)size);
        release_all();
        return false;
    }
    g_blob_size = size;

    g_engine = new engine::Engine();
    g_pkg = new engine::GamePackage();
    if (!g_pkg->parse(g_blob, g_blob_size)) {
        Serial.println("[game_runtime] .gbn 解析失败(格式或版本不对)");
        release_all();
        return false;
    }

    // 素材直接来自包内, 不读 assets-packed/
    g_engine->assets.set_fs(g_pkg);
    g_engine->load_assets("/");

    // 在宿主已有的 LVGL 屏幕上建 canvas (不注册新的显示驱动)
    if (!g_engine->display.init()) {
        Serial.println("[game_runtime] canvas 创建失败");
        release_all();
        return false;
    }

    g_game = new engine::ScriptGame();
    if (!g_game->init(g_pkg, gbn_path)) {
        Serial.println("[game_runtime] 脚本绑定失败");
        release_all();
        return false;
    }

    if (cfg.audio != nullptr) {
        g_engine->audio.attach(cfg.audio);
        g_engine->audio.start();
    }

    g_cfg = cfg;
    g_last_ms = millis();
    g_active = true;

    g_game->on_start(*g_engine);       // 跑 init / start 段
    Serial.printf("[game_runtime] 已加载 %s (%u 字节)\r\n", gbn_path, (unsigned)size);
    return true;
}

void stop() {
    if (!g_active && g_engine == nullptr) return;
    if (g_engine != nullptr && g_cfg.audio != nullptr) g_engine->audio.stop();
    release_all();
    g_active = false;
    g_cfg = Config();
}

bool active() { return g_active; }

void tick() {
    if (!g_active || g_engine == nullptr || g_game == nullptr) return;

    const uint32_t now = millis();
    const uint32_t elapsed = now - g_last_ms;
    const uint32_t frame_ms = (g_cfg.fps > 0) ? (1000u / (uint32_t)g_cfg.fps) : 16u;
    if (elapsed < frame_ms) return;             // 还没到下一帧
    g_last_ms = now;

    float dt = (float)elapsed / 1000.0f;
    if (dt > 0.25f) dt = 0.25f;                 // 卡顿时别让逻辑跳太远

    if (g_cfg.on_frame != nullptr && !g_cfg.on_frame()) {
        stop();
        return;
    }

    // 输入: 由宿主提供位图, 运行库自己算边沿
    const uint16_t held = g_cfg.read_pad();
    g_engine->input.held = held;
    g_engine->input.pressed = (uint16_t)(held & ~g_prev_held);
    g_engine->input.released = (uint16_t)(~held & g_prev_held);
    g_prev_held = held;

    g_engine->frame(*g_game, dt);               // update + render + present

    if (g_game->quit_requested()) stop();
}

} // namespace game_runtime
