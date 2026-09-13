#pragma once
#include "Game.h"
#include "AssetStore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// ============================================================================
// .gbn 游戏包 —— 字节码 + 嵌入素材, 格式见 docs/SCRIPT.md
//
// 加载后本身就是一个 FileSystem, 所以可以直接喂给 AssetStore:
//   engine.assets.set_fs(&pkg);
// 素材名用打平后的 basename("player.img"), 与包内记录一致。
// ============================================================================
class GamePackage : public FileSystem {
public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    // 解析内存里的整包。**不拷贝**素材, 所以 data 必须比本对象活得久。
    bool parse(const uint8_t* data, size_t size);

    // 从外部文件系统读入整包(内容复制进内存, 之后可释放来源数据)
    bool load_from(FileSystem& fs, const std::string& path);

    // ---- 元信息 ----
    uint16_t global_count() const { return globals_; }
    const char* string_at(uint32_t index) const;
    uint32_t string_count() const { return string_count_; }
    uint32_t code_size() const { return code_size_; }
    uint32_t asset_count() const { return asset_count_; }
    uint32_t entry_start()  const { return entry_[0]; }
    uint32_t entry_update() const { return entry_[1]; }
    uint32_t entry_render() const { return entry_[2]; }
    uint32_t entry_init()   const { return init_pc_; }

    // ---- FileSystem: 按名字取包内素材 ----
    bool read(const std::string& path, std::vector<uint8_t>& out) override;

    const uint8_t* code() const { return code_; }
    size_t size() const { return blob_.size(); }
    bool loaded() const { return code_ != nullptr; }

    // 卡带里到底有没有字节码 —— 这是"脚本卡带"与"纯素材卡带"的分水岭。
    //   纯素材卡带(给原生 C++ 游戏用): codeSize = 1 (一个 HALT 占位), 四个入口全是 kNone
    //   脚本卡带(build_game.py 编译 game.gs): 有真字节码, 入口至少设置了一个
    // 固件/模拟器据此决定"跑 VM"还是"回落同名原生游戏"。
    bool has_code() const {
        return code_size_ > 1 || init_pc_ != kNone ||
               entry_[0] != kNone || entry_[1] != kNone || entry_[2] != kNone;
    }

private:
    const uint8_t* find_asset(const std::string& name, uint32_t& out_size) const;

    std::vector<uint8_t> blob_;        // load_from 时持有整包
    const uint8_t* code_ = nullptr;
    uint32_t code_size_ = 0;
    uint16_t globals_ = 0;
    uint32_t init_pc_ = kNone;
    uint32_t entry_[3] = {kNone, kNone, kNone};

    // 字符串表
    uint32_t string_count_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> strings_;   // (偏移, 长度)
    std::vector<std::string> texts_;                       // NUL 结尾的副本(见 string_at)
    const uint8_t* str_base_ = nullptr;

    // 素材表
    uint32_t asset_count_ = 0;
    struct AssetRef { std::string name; uint32_t offset; uint32_t size; };
    std::vector<AssetRef> assets_;
};

// ============================================================================
// 用 .gbn 驱动的 Game: 每帧跑 update 段, 再跑 render 段。
// ============================================================================
class ScriptGame : public Game {
public:
    // 绑定包(不拥有)。name 用于调试/存档。
    bool init(const GamePackage* pkg, const char* name);

    const char* name() const override { return name_.c_str(); }
    void on_start(Engine& e) override;
    void on_update(Engine& e, float dt) override;
    void on_render(Engine& e) override;

    bool quit_requested() const { return quit_; }

private:
    int32_t run(Engine& e, uint32_t pc);
    int32_t builtin_num(Engine& e, uint8_t fn, const int32_t* args, int argc);
    // 把脚本里的资源名("star")转成 AssetStore 认的路径("star.img"), 避免每帧分配
    const char* asset_name(const char* raw, const char* ext);
    // 按字符串下标找贴图, 结果缓存(找不到也缓存住, 否则每帧都要重查)
    const Image* cached_image(Engine& e, uint32_t strIdx);
    // IMG / IMGF / IMGV 的公共实现: strIdx 已定, flip 来自栈
    void draw_indexed(Engine& e, uint32_t strIdx, int x, int y, bool flip);

    const GamePackage* pkg_ = nullptr;
    std::string name_ = "script";
    std::vector<int32_t> globals_;      // 同时是标量变量与数组内存池(见 docs/SCRIPT.md)
    std::vector<const Image*> img_cache_;
    std::vector<uint8_t>      img_done_;
    int32_t stack_[256] = {0};
    char    nbuf_[96] = {0};
    float   dt_ms_ = 16.0f;
    float   dt_s_  = 0.016f;
    uint32_t frame_ = 0;
    bool    quit_ = false;
};

} // namespace engine
