# 引擎 API

游戏代码只 include `engine/Engine.h`（它会带出其余头文件）。所有类型在 `namespace engine`。

## 生命周期

```cpp
class Game {
public:
    virtual const char* name() const = 0;          // 注册名
    virtual void on_start(Engine& e) = 0;          // 进入游戏时一次
    virtual void on_update(Engine& e, float dt) = 0;// 每逻辑帧(dt 秒, 已钳制上限)
    virtual void on_render(Engine& e) = 0;         // 每帧绘制
};
```

## Engine（门面）

```cpp
class Engine {
public:
    Display    display;   // 显示
    AssetStore assets;    // 素材
    Audio      audio;     // 音频
    PadState   input;     // 本帧手柄输入(见 docs/INPUT.md)

    void  quit();                              // 退出游戏/模拟器
    const Image* img(const char* name) const;  // 读图(打包名, 无扩展)
    const Clip*  snd(const char* name) const;  // 读音频
};
```

## Display（像素 + 图形 + 文字 + 贴图）

```cpp
uint16_t width() const;  uint16_t height() const;
void clear(Color c);
void set_pixel(int x, int y, Color c);
void fill_rect(int x, int y, int w, int h, Color c);
void draw_rect(int x, int y, int w, int h, Color c);
void hline(int x, int y, int w, Color c);
void vline(int x, int y, int h, Color c);
void line(int x0, int y0, int x1, int y1, Color c);
void draw_image(int x, int y, const Image& img);                 // 不透明贴图
void draw_image_alpha(int x, int y, const Image& img, Color key);// 跳过 key 色(透明)
void text(int x, int y, const char* s, Color c, int scale = 1);  // 内置 5x7 字体
void present();   // 提交本帧(Engine::frame 里已自动调用)
```

颜色：`Color` 即 RGB565 的 `uint16_t`，用 `rgb565(r,g,b)` 构造（每个分量 0..255）。

## Input

```cpp
bool is_held(e.input, Button::A);
bool is_pressed(e.input, Button::A);   // 本帧上升沿
bool is_released(e.input, Button::A);
```
方向键习惯性也提供组合判断：`if (is_held(e.input, Button::Left)) x -= v*dt;`

## Audio（音乐 + 音效同时出声，内部软混音）

```cpp
audio.play_bgm(e.snd("skyraider/bgm"), 0.9f);   // 循环播放 BGM
audio.stop_bgm();
audio.play_sfx(e.snd("skyraider/sfx_shoot"), 1.0f); // 一次音效(可与 BGM 叠加)
audio.set_master_volume(0.8f);                   // 0..1
```

## AssetStore

```cpp
const Image* img(const char* name) const;  // Image { w, h, data(RGB565) }
const Clip*  snd(const char* name) const;  // Clip  { frames, rate, data(int16 mono) }
```

## 引擎常量

- 逻辑分辨率 = 平台分辨率（模拟器 480×320，由 `lv_conf.h` 决定）。
- 音频采样率 = 22050 Hz（打包器保证素材同率）。
- 帧率由平台驱动（模拟器约 60fps；`dt` 已钳制，游戏逻辑一律用 `dt` 步进）。
