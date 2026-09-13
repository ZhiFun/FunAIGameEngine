#pragma once
#include <string>

// ============================================================================
// 素材 / 卡带的定位
//
// 这里踩过一个非常隐蔽的坑: 模拟器双击运行、或者从别的目录启动时, 工作目录
// 不是工程根目录, 相对路径 "games-bin/<游戏>.gbn" 就找不到了。旧代码会**静默**
// 往下回落到内嵌素材表(那是 skyraider 的 36 张图), fighter 于是只剩程序画出来
// 的背景, 人物/道具全不见 —— 看起来像渲染坏了, 其实一张图都没加载。
//
// 所以: 路径一律从可执行文件目录 / 当前目录往上找工程根目录再拼绝对路径,
// 找不到要让调用方知道(返回空串), 由调用方大声报错。
// ============================================================================
namespace sim {

// 工程根目录 = 含 games-bin/ 或 assets-packed/ 的那一层。找不到返回空串。
std::string project_root();

// 某个游戏卡带的绝对路径; 找不到返回空串。
// 顺序: 环境变量 FUNSIM_CART_DIR/<id>.gbn -> <工程根>/games-bin/<id>.gbn -> 工作目录。
std::string find_cart(const char* game_id);

// 素材目录 assets-packed 的绝对路径; 找不到返回空串。
// 顺序: 环境变量 FUNSIM_ASSETS_DIR -> <工程根>/assets-packed -> 工作目录。
std::string find_assets_dir();

} // namespace sim
