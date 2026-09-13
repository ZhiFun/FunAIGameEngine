// FunAIGameEngine - ESP32-S3 主程序(Arduino 骨架)
// 初始化 显示(LVGL+Arduino_GFX) / 音频(I2S 混音) / 素材(SPIFFS) / 输入(GPIO),
// 然后跑 games/registry.cpp 里注册的游戏。按键映射在 kKeys[]。
#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <vector>

#include "engine/Engine.h"
#include "games/registry.h"
#include "esp_display.h"
#include "esp_audio.h"

// ---- 按键: GPIO -> 标准手柄(低电平按下, 板子带内部上拉) ----
struct KeyMap { int gpio; engine::Button btn; };
static const KeyMap kKeys[] = {
    { 1, engine::Button::Up },    { 2, engine::Button::Down },
    { 3, engine::Button::Left },  { 4, engine::Button::Right },
    { 5, engine::Button::A },     { 6, engine::Button::B },
    { 7, engine::Button::C },     { 8, engine::Button::D },
    { 9, engine::Button::Start },  { 10, engine::Button::Select },
};

// ---- SPIFFS -> engine::FileSystem ----
// SPIFFS 没有子目录: 把 /skyraider/player.img 这类路径拍平成 skyraider_player.img。
namespace {
struct SpiffsFs : engine::FileSystem {
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        std::string flat = path;
        if (!flat.empty() && flat[0] == '/') flat.erase(flat.begin());
        for (auto& ch : flat) {
            if (ch == '/') ch = '_';
        }
        File f = SPIFFS.open(flat.c_str(), "r");
        if (!f) return false;
        out.resize(f.size());
        if (!out.empty()) f.read(out.data(), out.size());
        f.close();
        return true;
    }
};
} // namespace

static engine::Engine     g_engine;
static engine::Game*      g_game   = nullptr;
static esp_hal::I2sBackend g_audio;

static uint16_t g_held = 0, g_pressed = 0, g_released = 0;

static void scan_pad() {
    g_pressed = g_released = 0;
    uint16_t held = 0;
    for (auto& k : kKeys) {
        if (digitalRead(k.gpio) == LOW) held |= (uint16_t)(1u << (uint8_t)k.btn);
    }
    g_pressed  = held & (uint16_t)~g_held;
    g_released = (uint16_t)~held & g_held;
    g_held     = held;
}

void setup() {
    Serial.begin(115200);
    SPIFFS.begin(true);
    for (auto& k : kKeys) pinMode(k.gpio, INPUT_PULLUP);

    lv_init();
    esp_hal::display_init();

    static SpiffsFs fs;
    g_engine.assets.set_fs(&fs);
    g_engine.load_assets("/");       // SPIFFS 根(素材在 assets-packed 打平后放进 data/)
    g_engine.display.init();
    g_engine.audio.attach(&g_audio);
    g_engine.audio.start();

    g_game = games::create_game(games::default_game_name());
    if (g_game) g_game->on_start(g_engine);
    Serial.println("FunAIGameEngine ready");
}

void loop() {
    scan_pad();
    g_engine.input.held     = g_held;
    g_engine.input.pressed  = g_pressed;
    g_engine.input.released = g_released;

    lv_timer_handler();

    static uint32_t last = millis();
    const uint32_t now = millis();
    float dt = (float)(now - last) / 1000.0f;
    last = now;
    if (dt > 0.25f) dt = 0.25f;

    if (g_game && g_engine.running) g_engine.frame(*g_game, dt);

    lv_tick_inc(2);
    delay(2);
}
