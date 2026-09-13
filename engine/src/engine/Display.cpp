#include "Display.h"
#include <cstdlib>
#include <cstring>
#include "lvgl.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace engine {

// ---------------------------------------------------------------------------
// 内置 5x7 字体(经典 Adafruit glcdfont 风格, 公有领域)。
// 只收录 0x20..0x5F; 小写字母按大写处理, 其余未知字符按空格。
// 每字符 5 字节 = 5 列, 每个字节 bit0..bit6 = 从上到下 7 行。
// ---------------------------------------------------------------------------
static const uint8_t FONT5x7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x00,0x08,0x14,0x22,0x41}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x41,0x22,0x14,0x08,0x00}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
};

static const uint8_t* glyph_of(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');   // 小写按大写
    if (c >= 0x20 && c <= 0x5F) return FONT5x7[(unsigned)(c - 0x20)];
    return FONT5x7[0];                                     // 空格
}

// ---------------------------------------------------------------------------

bool Display::init() {
    w_ = LV_HOR_RES;
    h_ = LV_VER_RES;
    const size_t bytes = (size_t)w_ * h_ * 2;
    // 设备上一定要放 PSRAM: 内部 RAM 只有 320KB, 而键盘 UI/LVGL 还要用,
    // 抽走 121KB 会把显示队列挤爆(日志里能看到 display queue full)。
#if defined(ESP_PLATFORM)
    Color* buf = (Color*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (buf == nullptr) buf = (Color*)malloc(bytes);      // 没 PSRAM 时退回内部堆
#else
    Color* buf = (Color*)malloc(bytes);
#endif
    if (buf == nullptr) return false;

    canvas_ = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer((lv_obj_t*)canvas_, buf, w_, h_, LV_IMG_CF_TRUE_COLOR);
    // 显式把对象撑到全屏: 不设的话 canvas 对象尺寸由 LVGL 自己推断,
    // 刷新区可能小于缓冲区, 导致部分绘制上不了屏。
    lv_obj_set_pos((lv_obj_t*)canvas_, 0, 0);
    lv_obj_set_size((lv_obj_t*)canvas_, w_, h_);
    lv_img_dsc_t* img = lv_canvas_get_img((lv_obj_t*)canvas_);
    buf_ = (Color*)img->data;
    clear(0);
    return true;
}

void Display::clear(Color c) {
    fill_rect(0, 0, (int)w_, (int)h_, c);
}

void Display::set_pixel(int x, int y, Color c) {
    if (buf_ == nullptr || x < 0 || y < 0 || x >= w_ || y >= h_) return;
    buf_[(uint32_t)y * w_ + x] = c;
    mark(x, y, 1, 1);
}

void Display::fill_rect(int x, int y, int w, int h, Color c) {
    if (buf_ == nullptr || w <= 0 || h <= 0) return;
    if (x >= (int)w_ || y >= (int)h_) return;
    mark(x, y, w, h);
    int x2 = x + w, y2 = y + h;
    if (x2 <= 0 || y2 <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (int)w_) x2 = (int)w_;
    if (y2 > (int)h_) y2 = (int)h_;

    // 逐像素存 16bit 太慢(整屏 clear 一帧要几十万次), 改成按行 32bit 填:
    // 行首 4 字节对齐的字用一次写两个像素, 头尾剩余的单像素补齐。
    const int      n   = x2 - x;
    const uint32_t pat = (uint32_t)c | ((uint32_t)c << 16);
    for (int yy = y; yy < y2; ++yy) {
        Color* d = buf_ + (size_t)yy * w_ + x;
        int i = 0;
        if ((((uintptr_t)d) & 3u) == 0) {
            uint32_t*      dw = (uint32_t*)d;
            const int      nw = n >> 1;
            for (int k = 0; k < nw; ++k) dw[k] = pat;
            i = nw << 1;
        }
        for (; i < n; ++i) d[i] = c;
    }
}

void Display::draw_rect(int x, int y, int w, int h, Color c) {
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void Display::hline(int x, int y, int w, Color c) { fill_rect(x, y, w, 1, c); }
void Display::vline(int x, int y, int h, Color c) { fill_rect(x, y, 1, h, c); }

void Display::line(int x0, int y0, int x1, int y1, Color c) {
    const int dx = x1 - x0 < 0 ? -(x1 - x0) : x1 - x0;
    const int dy = y1 - y0 < 0 ? -(y1 - y0) : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        set_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// 索引一个像素: 调色板非空就是 4bpp 索引(0 = 透明), 否则是 RGB565(品红 = 透明)。
// 返回 -1 表示透明。flip_x 时先把 x 镜像到源图坐标。
static inline int image_pixel(const Image& img, int x, int y, bool flip_x = false) {
    if (flip_x) x = (int)img.w - 1 - x;
    if (img.pal != nullptr) {
        const size_t i = (size_t)y * img.w + (size_t)x;
        const uint8_t b = img.data[i >> 1];
        const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
        return (v == 0) ? -1 : (int)img.pal[v];
    }
    const uint8_t* p = img.data + ((size_t)y * img.w + (size_t)x) * 2;
    const uint16_t c = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    return (c == kColorMagenta) ? -1 : (int)c;
}

void Display::draw_image(int x, int y, const Image& img, bool flip_x) {
    if (buf_ == nullptr || img.data == nullptr) return;
    const int w = img.w, h = img.h;
    mark(x, y, w, h);
    if (img.pal != nullptr) {
        // 4bpp 索引图: 逐像素查调色板(索引 0 跳过)
        for (int yy = 0; yy < h; ++yy) {
            const int sy = y + yy;
            if (sy < 0 || sy >= (int)h_) continue;
            Color* dst = buf_ + (size_t)sy * w_;
            for (int xx = 0; xx < w; ++xx) {
                const int dx = x + xx;
                if (dx < 0 || dx >= (int)w_) continue;
                const int c = image_pixel(img, xx, yy, flip_x);
                if (c < 0) continue;
                dst[dx] = (Color)c;
            }
        }
        return;
    }
    if (flip_x) {
        // 要镜像就不能整行 memcpy, 退回逐像素(源列反向)
        const int sw0 = (x < 0) ? -x : 0;
        const int sw1 = (x + w > (int)w_) ? (int)w_ - x : w;
        for (int yy = 0; yy < h; ++yy) {
            const int sy = y + yy;
            if (sy < 0 || sy >= (int)h_) continue;
            const uint16_t* row = (const uint16_t*)(img.data + (size_t)yy * w * 2);
            Color*          dst = buf_ + (size_t)sy * w_;
            for (int xx = sw0; xx < sw1; ++xx) dst[x + xx] = row[w - 1 - xx];
        }
        return;
    }
    // 不透明贴图: 逐行 memcpy(原来逐像素拼 16bit, 大图非常慢)
    for (int yy = 0; yy < h; ++yy) {
        const int sy = y + yy;
        if (sy < 0 || sy >= (int)h_) continue;
        int sx = x, sw = w, skip = 0;
        if (sx < 0) { skip = -sx; sx = 0; sw += x; }
        if (sx + sw > (int)w_) sw = (int)w_ - sx;
        if (sw <= 0) continue;
        memcpy(buf_ + (size_t)sy * w_ + sx,
               img.data + ((size_t)yy * w + skip) * 2,
               (size_t)sw * 2);
    }
}

void Display::draw_image_alpha(int x, int y, const Image& img, Color key, bool flip_x) {
    if (buf_ == nullptr || img.data == nullptr) return;
    const int w = img.w, h = img.h;
    mark(x, y, w, h);
    if (img.pal != nullptr) {
        draw_image(x, y, img, flip_x);   // 索引图自带透明(索引 0), 不用键控
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        const int sy = y + yy;
        if (sy < 0 || sy >= (int)h_) continue;
        const uint16_t* row = (const uint16_t*)(img.data + (size_t)yy * w * 2);
        Color*          dst = buf_ + (size_t)sy * w_;
        int sx = x, xx = 0, n = w;
        if (sx < 0) { xx = -sx; n += x; sx = 0; }
        if (sx + n > (int)w_) n = (int)w_ - sx;
        for (int k = 0; k < n; ++k) {
            const uint16_t c = row[flip_x ? (w - 1 - (xx + k)) : (xx + k)];
            if (c == key) continue;                 // 透明色
            dst[sx + k] = c;
        }
    }
}

void Display::text(int x, int y, const char* s, Color c, int scale) {
    if (s == nullptr || buf_ == nullptr) return;
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char* p = s; *p; ++p) {
        if (*p == '\n') { cx = x; y += 8 * scale; continue; }
        const uint8_t* g = glyph_of(*p);
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = g[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    fill_rect(cx + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        cx += 6 * scale;
    }
}

// 脏区累加(带裁剪到画布范围内)
void Display::mark(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || w_ == 0 || h_ == 0) return;
    int x2 = x + w - 1, y2 = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (int)w_ - 1) x2 = (int)w_ - 1;
    if (y2 > (int)h_ - 1) y2 = (int)h_ - 1;
    if (x > x2 || y > y2) return;
    if (dx2_ < dx1_) { dx1_ = x; dy1_ = y; dx2_ = x2; dy2_ = y2; return; }   // 首块
    if (x  < dx1_) dx1_ = x;
    if (y  < dy1_) dy1_ = y;
    if (x2 > dx2_) dx2_ = x2;
    if (y2 > dy2_) dy2_ = y2;
}

void Display::present() {
    if (dx2_ < dx1_) return;                       // 本帧什么都没画, 不用刷
    const int x = dx1_, y = dy1_;
    const int w = dx2_ - dx1_ + 1, h = dy2_ - dy1_ + 1;
    dx1_ = dy1_ = 1;  dx2_ = dy2_ = 0;            // 复位成空矩形

    if (hook_ != nullptr) {                        // 平台自管上屏(键盘固件)
        if (buf_ != nullptr) hook_(hookCtx_, buf_, (int)w_, x, y, w, h);
        return;
    }
    if (canvas_ == nullptr) return;
    lv_area_t a;
    a.x1 = x;  a.y1 = y;
    a.x2 = x + w - 1;  a.y2 = y + h - 1;
    lv_obj_invalidate_area((lv_obj_t*)canvas_, &a);   // 只失效这一块
}

void Display::present_all() {
    if (canvas_ != nullptr) lv_obj_invalidate((lv_obj_t*)canvas_);
    dx1_ = dy1_ = 1;
    dx2_ = dy2_ = 0;
}

void Display::deinit() {
    // 注意: 缓冲是 heap_caps_malloc/malloc 出来的, canvas 对象归 LVGL 管
    if (canvas_ != nullptr) {
        lv_obj_del((lv_obj_t*)canvas_);
        canvas_ = nullptr;
    }
    if (buf_ != nullptr) {
        free(buf_);
        buf_ = nullptr;
    }
    w_ = 0;
    h_ = 0;
    dx1_ = dy1_ = 1;
    dx2_ = dy2_ = 0;
}

} // namespace engine
