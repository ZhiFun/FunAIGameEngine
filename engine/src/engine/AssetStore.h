#pragma once
#include "Types.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// 一张打包好的图
//   IMG1: RGB565, data = w*h*2 字节, 透明色 = 品红(chroma-key)
//   IMG2: 4bpp 索引, data = ceil(w*h/2) 字节, pal 非空, 索引 0 固定是透明
// 4bpp 是照 FC 的 CHR 做的: 锁死 16 色 → 体积减半, 而且不用再比色键。
struct Image {
    uint16_t      w = 0;
    uint16_t      h = 0;
    const uint8_t* data = nullptr;
    const Color*  pal  = nullptr;    // 非空 = 4bpp 索引图(索引 0 透明)
};

// 一段打包好的音频(单声道 int16)
struct Clip {
    uint32_t      frames = 0;
    uint32_t      rate   = 0;        // 引擎固定 22050
    const int16_t* data  = nullptr;
};

// 平台文件读取抽象(模拟器=磁盘, 硬件=SPIFFS/LittleFS)
class FileSystem {
public:
    virtual ~FileSystem() {}
    virtual bool read(const std::string& path, std::vector<uint8_t>& out) = 0;
};

// 内部缓存条目(保证返回的指针在加载后稳定)
struct ImageEntry {
    uint16_t w = 0, h = 0;
    std::vector<uint8_t> px;
    std::vector<Color>   pal;       // 4bpp 时用(16 项)
    Image view;
};
struct ClipEntry {
    std::vector<int16_t> data;
    Clip view;
};

// 从 assets-packed 目录按名字加载 .img / .snd(懒加载 + 缓存)
class AssetStore {
public:
    void set_fs(FileSystem* fs) { fs_ = fs; }
    bool init(const std::string& dir);   // 只需设置目录; 真正读取按名字懒加载
    void clear();

    const Image* image(const std::string& name) const;
    const Clip*  clip(const std::string& name) const;

private:
    FileSystem* fs_ = nullptr;
    std::string dir_;
    mutable std::unordered_map<std::string, ImageEntry> images_;
    mutable std::unordered_map<std::string, ClipEntry>  clips_;
};

} // namespace engine
