#include "RomFileSystem.h"

#include <cstddef>
#include <cstring>

// 生成的内嵌素材表(tools/embed_assets.py 产出)。没生成时下面的空表兜底,
// 这样"忘了生成"只会退化成用文件系统, 而不是编译失败。
#if defined(__has_include)
#  if __has_include("rom_assets_data.h")
#    include "rom_assets_data.h"
#    define FUN_HAS_ROM_ASSETS 1
#  endif
#endif

#ifndef FUN_HAS_ROM_ASSETS
#define FUN_HAS_ROM_ASSETS 0
namespace engine {
namespace rom {
struct Entry {
    const char*    name;
    const uint8_t* data;
    uint32_t       size;
};
static const Entry    kTable[] = { { "", nullptr, 0 } };
static const uint32_t kCount   = 0;
} // namespace rom
} // namespace engine
#endif

namespace engine {

namespace {

// 把各种写法归一化成查表用的键: 去掉开头的 '/'(含重复的)、'\\' 换成 '/'、去掉尾部 '/'。
// AssetStore 传进来的实际是 dir_ + "/" + name, dir_ 为 "" 或 "/" 时会多出斜杠。
std::string normalize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '\\') {
            if (!out.empty() && out.back() != '/') out.push_back('/');
            continue;
        }
        if (c == '/') {
            if (out.empty() || out.back() == '/') continue;   // 丢掉开头的和连续的
            out.push_back('/');
            continue;
        }
        out.push_back(c);
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

} // namespace

int RomFileSystem::count() const { return (int)rom::kCount; }

bool RomFileSystem::read(const std::string& path, std::vector<uint8_t>& out) {
    const std::string key = normalize(path);
    if (key.empty()) return false;

    // 查表: 先拿归一化后的完整路径, 再拿 basename 试一次。
    // 两种键都可能出现:
    //   · 表由 assets-packed/ 目录生成  -> "skyraider/player.img"
    //   · 表由 .gbn 卡带生成           -> "player.img"(卡带里存的就是打平后的基名)
    // 而 AssetStore 传进来的是 dir_ + "/" + name + 扩展名, 所以必须两种都支持。
    static const auto find = [](const std::string& k) -> const rom::Entry* {
        for (uint32_t i = 0; i < rom::kCount; ++i) {
            const rom::Entry& e = rom::kTable[i];
            if (e.name == nullptr || e.data == nullptr) continue;
            if (std::strcmp(e.name, k.c_str()) == 0) return &e;
        }
        return nullptr;
    };

    const rom::Entry* hit = find(key);
    if (hit == nullptr) {
        const size_t slash = key.find_last_of('/');
        if (slash != std::string::npos) hit = find(key.substr(slash + 1));
    }
    if (hit == nullptr) return false;

    out.assign(hit->data, hit->data + hit->size);
    return true;
}

RomFileSystem& rom_fs() {
    static RomFileSystem fs;
    return fs;
}

} // namespace engine
