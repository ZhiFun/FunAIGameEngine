#include "AssetStore.h"
#include <cstring>

namespace engine {

// ---------------------------------------------------------------------------
// 小端读取
// ---------------------------------------------------------------------------
static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ---------------------------------------------------------------------------
// 补扩展名。
// ⚠ 名字**可能已经带扩展名**了 —— 脚本/卡带侧的约定就是 "player.img"/"coin.snd"
//   (GamePackage 里记录的就是带扩展名的名字)。直接往后拼会变成 "player.img.img",
//   于是所有的图/声都不声不响地找不到。踩过。
// ---------------------------------------------------------------------------
static std::string with_ext(const std::string& name, const char* ext) {
    const size_t n = strlen(ext);
    if (name.size() > n + 1 &&
        name.compare(name.size() - n, n, ext) == 0 &&
        name[name.size() - n - 1] == '.') {
        return name;
    }
    return name + "." + ext;
}

// ---------------------------------------------------------------------------
bool AssetStore::init(const std::string& dir) {
    dir_ = dir;
    return fs_ != nullptr;
}

void AssetStore::clear() {
    images_.clear();
    clips_.clear();
}

const Image* AssetStore::image(const std::string& name) const {
    auto it = images_.find(name);
    if (it != images_.end()) return &it->second.view;

    if (fs_ == nullptr) return nullptr;
    std::vector<uint8_t> bytes;
    if (!fs_->read(dir_ + "/" + with_ext(name, "img"), bytes)) return nullptr;
    if (bytes.size() < 8) return nullptr;

    ImageEntry e;
    if (memcmp(bytes.data(), "IMG2", 4) == 0) {
        // 4bpp 索引图: 调色板固定 16 项(32 字节), 索引 0 一定是透明
        e.w = rd16(bytes.data() + 4);
        e.h = rd16(bytes.data() + 6);
        const size_t need = ((size_t)e.w * e.h + 1) / 2;
        if (bytes.size() < 40 + need) return nullptr;
        e.pal.resize(16);
        for (int i = 0; i < 16; ++i) e.pal[i] = rd16(bytes.data() + 8 + i * 2);
        e.px.assign(bytes.begin() + 40, bytes.begin() + 40 + need);
        e.view = Image{e.w, e.h, e.px.data(), e.pal.data()};
    } else if (memcmp(bytes.data(), "IMG1", 4) == 0) {
        e.w  = rd16(bytes.data() + 4);
        e.h  = rd16(bytes.data() + 6);
        const size_t need = (size_t)e.w * e.h * 2;
        if (bytes.size() < 8 + need) return nullptr;
        e.px.assign(bytes.begin() + 8, bytes.begin() + 8 + need);
        e.view = Image{e.w, e.h, e.px.data(), nullptr};
    } else {
        return nullptr;
    }

    auto res = images_.emplace(name, std::move(e));
    return &res.first->second.view;
}

const Clip* AssetStore::clip(const std::string& name) const {
    auto it = clips_.find(name);
    if (it != clips_.end()) return &it->second.view;

    if (fs_ == nullptr) return nullptr;
    std::vector<uint8_t> bytes;
    if (!fs_->read(dir_ + "/" + with_ext(name, "snd"), bytes)) return nullptr;
    if (bytes.size() < 12 || memcmp(bytes.data(), "SND1", 4) != 0) return nullptr;

    const uint32_t rate   = rd32(bytes.data() + 4);
    const uint32_t frames = rd32(bytes.data() + 8);
    if (bytes.size() < 12 + (size_t)frames * 2) return nullptr;

    ClipEntry e;
    e.data.resize(frames);
    const uint8_t* p = bytes.data() + 12;
    for (uint32_t i = 0; i < frames; ++i) {
        e.data[i] = (int16_t)rd16(p + i * 2);
    }
    e.view = Clip{frames, rate, e.data.data()};

    auto res = clips_.emplace(name, std::move(e));
    return &res.first->second.view;
}

} // namespace engine
