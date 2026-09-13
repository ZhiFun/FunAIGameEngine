#include <QApplication>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "launcher.h"
#include "sim_paths.h"
#include "sim_platform.h"
#include "sim_window.h"
#include "lvgl.h"

// 用法:
//   fun_sim                                    -> 启动器(选游戏 + 选硬件平台)
//   fun_sim <游戏> [素材目录] [--device id] [--zoom N]   -> 直接跑
//   fun_sim <xxx.gbn>                          -> 直接跑脚本游戏包
// 例: fun_sim fighter assets-packed --device keyboard --zoom 2
//
// ⚠ 选项名不要叫 --platform / --scale: Qt 自己就吃 -platform(平台插件) 和
//   -scale(高分屏缩放), 会把它们从命令行里抢走, 结果报
//   "Could not find the Qt platform plugin \"keyboard\"" 直接启动失败。
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    const char* game = nullptr;
    const char* assets = "assets-packed";
    const char* plat_id = nullptr;
    int scale = 0;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            plat_id = argv[++i];
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (positional == 0) {
            game = argv[i];
            positional = 1;
        } else if (positional == 1) {
            assets = argv[i];
            positional = 2;
        }
    }

    // 没指定游戏 -> 开启动器(不需要 LVGL, 也不建显示)
    if (game == nullptr) {
        sim::LauncherWindow win;
        win.show();
        return app.exec();
    }

    const sim::PlatformInfo* plat = sim::find_platform(plat_id);
    if (plat == nullptr) plat = sim::default_platform();

    // 素材/卡带在哪里 —— 从 exe 目录和当前目录往上找到含 games-bin/ 的工程根目录,
    // 所以双击运行、从任意目录启动都能找到卡带(以前用相对路径会静默加载不到素材)。
    const std::string root = sim::project_root();
    const std::string cart = sim::find_cart(game);
    fprintf(stderr, "[sim] 工程根: %s\n",
            root.empty() ? "(没找到, 只好用当前工作目录)" : root.c_str());
    fprintf(stderr, "[sim] 卡带  : %s\n",
            cart.empty() ? "(没找到! 素材会退到内嵌表/素材目录, 很可能没有这个游戏的图)"
                         : cart.c_str());
    fflush(stderr);

    // 分辨率/显示驱动在窗口构造里建, 建之前必须 lv_init()
    lv_init();
    sim::GameWindow win(game, assets, *plat, scale);
    win.show();
    win.activateWindow();   // 确保窗口拿到键盘焦点(否则按键无效)
    win.raise();
    return app.exec();
}

