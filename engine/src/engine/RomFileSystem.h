#pragma once
#include "AssetStore.h"

#include <string>

namespace engine {

// ============================================================================
// RomFileSystem —— 内嵌素材(卡带 ROM 模型)
//
// 为什么要有它:
//   用文件系统放素材(磁盘目录 / SPIFFS)会引入一整类"运行期才暴露"的故障:
//   目录没上传、文件被裁掉、路径拼错、分区被格式化…… 游戏本体没事, 素材却没了。
//   街机/FC/GBA 从来不这么干 —— 素材就在卡带 ROM 里, 通过地址直接访问,
//   游戏和素材同一个镜像, 不存在"找不到"。
//
// 本类就是那个 ROM 的角色:
//   tools/embed_assets.py 把 assets-packed/ 生成成 engine/src/rom_assets_data.h,
//   编译器把它变成 .rodata(ESP32 上就是 flash 里的常量), 这里按名字查表返回。
//   素材量级(本项目 ~370KB)远小于 flash, 代价只是镜像大一点。
//
// 平台用法:
//   if (engine::rom_fs().available()) { assets.set_fs(&engine::rom_fs()); assets.init(""); }
//   else                              { /* 退回磁盘/SPIFFS */ }
// ============================================================================
class RomFileSystem : public FileSystem {
public:
    // path 允许是 "skyraider/player.img"、"/skyraider/player.img"、
    // "//skyraider/player.img"(AssetStore 用 dir_+"/"+name 拼出来的), 都会归一化后再查。
    bool read(const std::string& path, std::vector<uint8_t>& out) override;

    // 内嵌表里有几个素材(为 0 说明 rom_assets_data.h 还没生成)
    int  count() const;

    // 有内嵌素材可用(平台据此决定要不要退回文件系统)
    bool available() const { return count() > 0; }
};

// 全局单例(表是常量, 无状态, 随便共享)
RomFileSystem& rom_fs();

} // namespace engine
