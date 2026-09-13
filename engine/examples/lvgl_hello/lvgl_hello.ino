// ---------------------------------------------------------------------------
// FunAIGameEngine 最小示例：LVGL + 引擎 + 一个写死在代码里的小游戏。
//
// 要点:
//   1) **先把 LVGL 拉起来**（display driver + draw buffer + flush_cb），
//      引擎的画布是挂在 LVGL 上的。
//   2) 再 display.init()，然后每帧 on_update / on_render / present。
//   3) 引擎不知道你的屏：flush_cb 里那块像素要你自己推给面板。
//
// 跑卡带(.gbn，游戏逻辑在文件里)见库 README §5；
// 完整参考实现(含 I2S 音频交接、切屏、退出清理)见键盘固件 src/game/game_port.cpp。
// ---------------------------------------------------------------------------
#include <lvgl.h>
#include <FunAIGameEngine.h>

// 与键盘实机同分辨率；换你自己的屏时改这两行
static const int kScrW = 428;
static const int kScrH = 142;

static lv_color_t          s_buf[kScrW * 20];        // 20 行的部分刷新缓冲
static lv_disp_draw_buf_t  s_drawBuf;
static lv_disp_drv_t       s_dispDrv;

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    // TODO: 把 (area, px) 这块推到你的屏(SPI / 并口 / LVGL 官方驱动)。
    //       引擎自己会调 present(), 推屏由这里负责。
    (void)area; (void)px;
    lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------------------
class Demo : public engine::Game {
public:
    const char* name() const override { return "demo"; }

    void on_start(engine::Engine& e) override {
        x_ = 40.0f;
        vx_ = 90.0f;
        (void)e;
    }

    void on_update(engine::Engine& e, float dt) override {
        const uint16_t L = engine::bit_of(engine::Button::Left);
        const uint16_t R = engine::bit_of(engine::Button::Right);
        if (e.input.held & L) x_ -= 140.0f * dt;
        if (e.input.held & R) x_ += 140.0f * dt;

        x_ += vx_ * dt;
        if (x_ < 2.0f)            { x_ = 2.0f;            vx_ = -vx_; }
        if (x_ > kScrW - 18.0f)   { x_ = kScrW - 18.0f;   vx_ = -vx_; }
    }

    void on_render(engine::Engine& e) override {
        using engine::rgb565;
        e.display.fill_rect(0, 0, kScrW, kScrH, rgb565(8, 10, 18));          // 背景
        e.display.fill_rect(0, 120, kScrW, 22, rgb565(72, 50, 42));          // 地面
        e.display.fill_rect((int)x_, 104, 16, 16, rgb565(255, 140, 60));     // 方块
        e.display.text(6, 6, "FunAIGameEngine", rgb565(150, 200, 255), 1);
        char buf[32];
        snprintf(buf, sizeof(buf), "v%s  h=%d", engine::kVersionString, (int)e.input.held);
        e.display.text(6, 16, buf, rgb565(120, 134, 160), 1);
    }

private:
    float x_ = 40.0f;
    float vx_ = 90.0f;
};

static engine::Engine g_engine;
static Demo           g_demo;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nFunAIGameEngine v%s\n", engine::kVersionString);

    // ---- 1) LVGL ----
    lv_init();
    lv_disp_draw_buf_init(&s_drawBuf, s_buf, nullptr, kScrW * 20);
    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res  = kScrW;
    s_dispDrv.ver_res  = kScrH;
    s_dispDrv.flush_cb = flush_cb;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);

    // ---- 2) 引擎 ----
    if (!g_engine.display.init()) {
        Serial.println("display.init() 失败");
        return;
    }
    g_engine.running = true;
    g_demo.on_start(g_engine);

    // 想出声: 实现一个 engine::AudioBackend 然后
    //   g_engine.audio.attach(&yourBackend);
}

void loop() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    uint32_t d = now - last;
    last = now;
    if (d > 50) d = 50;
    lv_tick_inc(d);                       // 单线程示例: 这里自己推进 LVGL 时基

    float dt = d / 1000.0f;
    if (dt > 0.25f) dt = 0.25f;

    // 手柄: 真实项目里从你的键盘矩阵/手柄读, 这里先塞点值方便看效果
    g_engine.input.held = 0;

    g_demo.on_update(g_engine, dt);
    g_demo.on_render(g_engine);
    g_engine.display.present();           // 没设 set_present_hook, 交给 LVGL 刷新

    lv_timer_handler();
    delay(5);
}
