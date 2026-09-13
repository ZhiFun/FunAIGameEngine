#pragma once
#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include <cstdint>
#include <string>

#include "engine/Engine.h"
#include "engine/Game.h"
#include "engine/ScriptGame.h"
#include "sim_audio.h"
#include "sim_platform.h"

// Windows 模拟器(Qt 版): QWidget 渲染 LVGL 全屏 canvas + 键盘 -> 标准手柄。
// 与硬件后端使用同一套 engine::PadState 语义。
//
// 分辨率来自 PlatformInfo(启动器里选的那台设备), 在窗口构造时定下:
// LVGL 8.3 的 LV_HOR_RES/LV_VER_RES 展开为 lv_disp_get_hor_res(lv_disp_get_default()),
// 显示驱动注册之前是 0, 拿它去配 display driver 会注册出 0x0 的显示器(窗口全黑)。
// 所以换分辨率 = 换进程: 启动器用子进程重开(见 launcher.cpp)。
namespace sim {

class GameWindow : public QWidget {
public:
    // gameName 为空则用默认游戏; assetsDir 默认 "assets-packed"
    explicit GameWindow(const char* gameName, const char* assetsDir,
                        const PlatformInfo& plat, int scale, QWidget* parent = nullptr);

    QImage& framebuffer() { return fb_; }        // 供 LVGL flush 回调写帧
    uint32_t lvKey() const;                      // 供 LVGL 键盘 indev 读

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void initEngine(const char* assetsDir, const char* pkgPath);
    void tick();
    static bool keyToButton(int qtKey, engine::Button& out);

    engine::Engine  engine_;
    engine::Game*   game_ = nullptr;
    sim::AudioBackend audio_;

    // .gbn 游戏包模式(传入的游戏名以 .gbn 结尾时启用)
    engine::GamePackage pkg_;
    engine::ScriptGame  script_;
    bool script_mode_ = false;

    // 本次运行用哪台设备 + 它的分辨率(建显示之前必须定下来)
    const PlatformInfo* plat_ = nullptr;
    int sw_ = 0, sh_ = 0;
    std::string game_id_;      // 注册名(C++ 游戏)或 .gbn 路径(脚本游戏)

    QImage  fb_;              // RGB565 帧缓冲(与引擎 Color 一致)
    uint16_t held_     = 0;   // 当前按住(engine::Button 位)
    uint16_t pressed_  = 0;   // 本帧上升沿
    uint16_t released_ = 0;   // 本帧下降沿

    QElapsedTimer clock_;
    int  scale_ = 2;          // 窗口放大倍数(像素风格, 最近邻)

    // 素材源诊断: 非空 = 素材没配对(比如只回落到内嵌表, 里面没有这个游戏的图)。
    // 窗口会画一条红色横幅 + 标题加标记 —— "只有背景没有人物"这种问题一眼就能定位。
    std::string assets_note_;
};

} // namespace sim


