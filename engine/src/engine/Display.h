#pragma once
#include "Types.h"
#include "AssetStore.h"

namespace engine {

// 全屏像素显示层: 一块 LVGL 全屏 canvas, 游戏直接往缓冲里画像素。
// 平台后端(LVGL 显示驱动)负责把 canvas 刷到真实屏幕 / SDL 窗口。
class Display {
public:
    bool init();                      // 按平台分辨率创建 canvas
    void deinit();                    // 释放 canvas 与缓冲(可再次 init)
    uint16_t width() const  { return w_; }
    uint16_t height() const { return h_; }

    void clear(Color c);
    void set_pixel(int x, int y, Color c);
    void fill_rect(int x, int y, int w, int h, Color c);
    void draw_rect(int x, int y, int w, int h, Color c);
    void hline(int x, int y, int w, Color c);
    void vline(int x, int y, int h, Color c);
    void line(int x0, int y0, int x1, int y1, Color c);

    // flip_x: 水平镜像。角色朝左/朝右用同一张图翻转, 省一半素材。
    // 索引图(4bpp)与 RGB565 都支持(镜像发生在像素取值前, 所以透明色键控不受影响)。
    void draw_image(int x, int y, const Image& img, bool flip_x = false);
    void draw_image_alpha(int x, int y, const Image& img, Color key, bool flip_x = false);

    void text(int x, int y, const char* s, Color c, int scale = 1);     // 内置 5x7 字体

    void present();                    // 只把本帧画过的区域标脏 -> LVGL 刷新
    void present_all();                // 无条件全屏刷新(切屏/首帧时用)

    // 平台"直接上屏"钩子: 非空时 present() 不再通知 LVGL, 而是把本帧脏矩形交给
    // 平台自己推给面板(键盘固件用它绕开 LVGL 的画布重绘 + 刷新调度)。
    // fn(ctx, 帧缓冲首地址, 行宽(像素), x, y, w, h)
    typedef void (*PresentHook)(void* ctx, const void* buf, int stride,
                                int x, int y, int w, int h);
    void set_present_hook(PresentHook fn, void* ctx) {
        hook_    = fn;
        hookCtx_ = ctx;
    }

    // 本帧脏矩形(诊断用: 看实际要重绘多大)
    int dirty_x() const { return dx1_; }
    int dirty_y() const { return dy1_; }
    int dirty_w() const { return (dx2_ >= dx1_) ? (dx2_ - dx1_ + 1) : 0; }
    int dirty_h() const { return (dy2_ >= dy1_) ? (dy2_ - dy1_ + 1) : 0; }

private:
    void mark(int x, int y, int w, int h);   // 脏区累加(带裁剪)

    void*  canvas_ = nullptr;          // lv_obj_t*
    Color* buf_    = nullptr;          // 指向 canvas 缓冲
    uint16_t w_ = 0, h_ = 0;
    // 脏矩形; dx2_ < dx1_ 表示"空"
    int dx1_ = 1, dy1_ = 1, dx2_ = 0, dy2_ = 0;

    PresentHook hook_    = nullptr;    // 平台自铺上屏(非空则不经过 LVGL)
    void*       hookCtx_ = nullptr;
};

} // namespace engine
