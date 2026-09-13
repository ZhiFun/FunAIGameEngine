// FunAIGameEngine - ESP32-S3 键盘硬件实例主程序
//
// 初始化 显示(TFT_eSPI+NV3007) / 音频(I2S1) / 素材(SPIFFS) / 输入(键盘矩阵),
// 然后跑 games/registry.cpp 里注册的游戏。
//
// 硬件: FunModularKeyboard (ESP32-S3, 428x142 NV3007, MAX98357 on I2S1)
#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <vector>

#include "engine/Engine.h"
#include "engine/RomFileSystem.h"
#include "games/registry.h"
#include "esp_display.h"
#include "esp_audio.h"

// ============================================================================
// 输入: 机身 5x4 键盘矩阵 -> 标准手柄
// 行列引脚与键盘工程 src/MatrixScanner.h 一致。改接线只改这两行。
// ============================================================================
static const uint8_t kRowPins[] = {48, 10, 47, 33, 14};   // 5 行
static const uint8_t kColPins[] = {35, 34, 7, 13};        // 4 列
static constexpr int kRows = sizeof(kRowPins) / sizeof(kRowPins[0]);
static constexpr int kCols = sizeof(kColPins) / sizeof(kColPins[0]);

// 第 n 个矩阵位置(row-major)对应的手柄按键; 不需要的填 Button::Count 之外的占位。
static const engine::Button kKeyMap[kRows * kCols] = {
    engine::Button::Up,    engine::Button::Down,  engine::Button::Left,  engine::Button::Right,  // row0
    engine::Button::A,     engine::Button::B,     engine::Button::X,     engine::Button::Y,      // row1
    engine::Button::C,     engine::Button::D,     engine::Button::L,     engine::Button::R,      // row2
    engine::Button::Start, engine::Button::Select, engine::Button::A,    engine::Button::B,      // row3
    engine::Button::Up,    engine::Button::Down,  engine::Button::Left,  engine::Button::Right,  // row4
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

static engine::Engine      g_engine;
static engine::Game*       g_game = nullptr;
static esp_hal::I2sBackend g_audio;

static uint16_t g_held = 0, g_pressed = 0, g_released = 0;

// 逐列拉低扫描(列做输出、行做输入上拉, 与键盘工程相反亦可, 这里按列驱动更省电)
static void scan_pad() {
    uint16_t held = 0;
    for (int c = 0; c < kCols; ++c) {
        pinMode(kColPins[c], OUTPUT);
        digitalWrite(kColPins[c], LOW);
        delayMicroseconds(30);
        for (int r = 0; r < kRows; ++r) {
            if (digitalRead(kRowPins[r]) == LOW) {
                const int index = r * kCols + c;
                const engine::Button b = kKeyMap[index];
                held |= (uint16_t)(1u << (uint8_t)b);
            }
        }
        pinMode(kColPins[c], INPUT);        // 释放为高阻, 避免多键同时按下时串扰
    }
    g_pressed  = held & (uint16_t)~g_held;
    g_released = (uint16_t)~held & g_held;
    g_held     = held;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    // 矩阵引脚
    for (int r = 0; r < kRows; ++r) pinMode(kRowPins[r], INPUT_PULLUP);
    for (int c = 0; c < kCols; ++c) pinMode(kColPins[c], INPUT);

    SPIFFS.begin(true);

    lv_init();
    esp_hal::display_init();

    // 素材: 优先用编译进固件的内嵌表(卡带 ROM 模型, 与 FC/GBA 同一思路)。
    // 这样刷一次固件就带全素材, 不再依赖 SPIFFS 里有没有 uploadfs 过。
    if (engine::rom_fs().available()) {
        g_engine.assets.set_fs(&engine::rom_fs());
        g_engine.load_assets("");
        Serial.printf("assets: embedded ROM, %d items\n", engine::rom_fs().count());
    } else {
        static SpiffsFs fs;
        g_engine.assets.set_fs(&fs);
        g_engine.load_assets("/");          // SPIFFS 根(素材打平后放进 data/)
        Serial.println("assets: SPIFFS fallback (embedded table empty)");
    }
    g_engine.display.init();
    g_engine.audio.attach(&g_audio);
    g_engine.audio.start();

    g_game = games::create_game(games::default_game_name());
    if (g_game) g_game->on_start(g_engine);
    Serial.println("FunAIGameEngine ready (esp32s3keyboard)");
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
