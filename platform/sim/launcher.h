#pragma once
#include <QWidget>

class QComboBox;
class QLabel;
class QProcess;

namespace sim {

// 模拟器启动器: 选「跑哪个游戏」+「用哪台硬件平台」, 然后用子进程拉起游戏窗口。
//
// 为什么要子进程: 逻辑分辨率必须在建 LVGL 显示器之前定下来(LV_HOR_RES 在
// 注册显示前返回 0, 见 sim_platform.h), 进程内改分辨率要拆掉重建显示器,
// 而 LVGL 8.3 的 lv_disp_remove 不会回收屏幕对象 —— 重开进程最干净。
// 好处顺带: 换游戏/平台不会把上一局的状态带过来。
class LauncherWindow : public QWidget {
    Q_OBJECT
public:
    explicit LauncherWindow(QWidget* parent = nullptr);

private:
    void build_ui();
    void populate_games();
    void launch();
    void stop_child();

    QComboBox* game_     = nullptr;
    QComboBox* platform_ = nullptr;
    QComboBox* scale_    = nullptr;
    QLabel*    status_   = nullptr;
    QProcess*  child_    = nullptr;
};

} // namespace sim
