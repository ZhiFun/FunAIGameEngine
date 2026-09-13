#pragma once
// sim_platform.h —— 模拟器里可选的"硬件平台"
//
// 一个平台 = 一种要跑的设备: 逻辑分辨率 + 默认窗口缩放。
// 与 platform/<id>/ 下的移植后端一一对应, 加一台设备就在这里加一条。
//
// ⚠ 分辨率必须在**建显示驱动之前**定下来: LVGL 8.3 的 LV_HOR_RES/LV_VER_RES
//   是 lv_disp_get_hor_res(lv_disp_get_default()), 注册之前返回 0。
//   换分辨率 = 换进程(启动器用子进程重开), 进程内不改。
namespace sim {

struct PlatformInfo {
    const char* id;        // 命令行: --device <id>(不能叫 --platform, Qt 会抢)
    const char* label;     // 界面显示
    int width, height;     // 逻辑分辨率(必须与硬件后端一致)
    int scale;             // 窗口默认放大倍数(像素风最近邻)
};

// 全部平台(顺序即界面顺序, 第一条是默认)
const PlatformInfo* platform_list(int* count);
// 按 id 查; 找不到返回 nullptr
const PlatformInfo* find_platform(const char* id);
// 默认平台(命令行没给 --platform 时用)
const PlatformInfo* default_platform();

} // namespace sim
