#include "sim_window.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "lvgl.h"
#include "games/registry.h"
#include "engine/RomFileSystem.h"
#include "sim_paths.h"

namespace sim {

// ---------------------------------------------------------------------------
static GameWindow* g_win = nullptr;

namespace {
struct DiskFs : engine::FileSystem {
    bool read(const std::string& path, std::vector<uint8_t>& out) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return !out.empty();
    }
};

// 传进来的游戏名是不是 .gbn 包路径
bool is_package_path(const char* s) {
    if (s == nullptr) return false;
    const size_t n = strlen(s);
    if (n < 4) return false;
    const char* tail = s + n - 4;
    return (tail[0] == '.') &&
           (tail[1] == 'g' || tail[1] == 'G') &&
           (tail[2] == 'b' || tail[2] == 'B') &&
           (tail[3] == 'n' || tail[3] == 'N');
}
} // namespace

// ---------------------------------------------------------------------------
// LVGL 显示驱动: 把 lvgl 刷出来的区域拷进 QImage 帧缓冲
// ---------------------------------------------------------------------------
static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    if (g_win) {
        const int w = area->x2 - area->x1 + 1;
        for (int y = area->y1; y <= area->y2; ++y) {
            const uint8_t* src = (const uint8_t*)color_p + (size_t)(y - area->y1) * w * 2;
            uint8_t* dst = g_win->framebuffer().scanLine(y) + (size_t)area->x1 * 2;
            memcpy(dst, src, (size_t)w * 2);
        }
    }
    lv_disp_flush_ready(drv);
}

static void indevReadCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static uint32_t last = 0;
    (void)drv;
    const uint32_t key = g_win ? g_win->lvKey() : 0;
    if (key != last) {
        data->state = key ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->key = key;
        last = key;
    }
}

// ---------------------------------------------------------------------------

// 调试输入问题时设 FUNSIM_KEYLOG=1, 每次按键会往 stderr 打一行(正常使用保持安静)
static bool keylog_enabled() {
    static const bool on = (getenv("FUNSIM_KEYLOG") != nullptr);
    return on;
}

GameWindow::GameWindow(const char* gameName, const char* assetsDir,
                       const PlatformInfo& plat, int scale, QWidget* parent)
    : QWidget(parent), plat_(&plat), sw_(plat.width), sh_(plat.height) {
    // 没显式给缩放就用该平台的默认值(480x320 再放大 2 倍就出屏了)
    scale_ = (scale > 0) ? scale : plat.scale;
    const char* raw = (gameName != nullptr) ? gameName : "";
    game_id_ = (raw[0] != '\0') ? raw : games::default_game_name();

    // 平台 label 里已经带分辨率了, 标题里不再重复拼一次
    setWindowTitle(QStringLiteral("FunAIGameEngine · %1 · %2 (%3x)")
                       .arg(QString::fromUtf8(game_id_.c_str()))
                       .arg(QString::fromUtf8(plat.label))
                       .arg(scale_));
    setFixedSize(sw_ * scale_, sh_ * scale_);

    // 关键: QWidget 默认是 Qt::NoFocus, 不设这个就永远收不到键盘事件
    // (窗口能显示但方向键/ABCD 完全无响应)
    setFocusPolicy(Qt::StrongFocus);
    setFocus();

    g_win = this;
    fb_ = QImage(sw_, sh_, QImage::Format_RGB16);
    fb_.fill(0);

    const bool want_pkg = is_package_path(raw);
    initEngine(assetsDir, want_pkg ? raw : nullptr);

    // 素材没配对时标题上也标一下 —— 光看画面"只有背景没人物"根本猜不到原因
    if (!assets_note_.empty()) {
        setWindowTitle(windowTitle() + QStringLiteral("   [素材缺失]"));
    }

    if (want_pkg) {
        // 显式给了 .gbn: 有字节码就跑 VM; 没有就说明这是纯素材卡带, 回落到同名原生游戏
        if (games::cart_has_script(&pkg_) && script_.init(&pkg_, raw)) {
            script_mode_ = true;
            game_ = &script_;
        }
    }
    if (game_ == nullptr) {
        // 统一入口: 卡带里带字节码 -> VM; 否则回落同名原生 C++ 游戏。
        // 固件那边用的是同一个函数, 所以 `fun_sim starfall` 与实机行为一致。
        game_ = games::create_from_cart(&pkg_, game_id_.c_str());
    }
    if (game_ == nullptr && want_pkg) {
        fprintf(stderr, "[sim] 无法加载游戏包: %s\n", raw);
        fflush(stderr);
    }
    if (game_) game_->on_start(engine_);

    clock_.start();
    auto* timer = new QTimer(this);
    QObject::connect(timer, &QTimer::timeout, this, &GameWindow::tick);
    timer->start(16);
}

void GameWindow::initEngine(const char* assetsDir, const char* pkgPath) {
    // LVGL 显示驱动(分辨率来自所选平台, 每进程只建一次)
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t* buf1 =
        (lv_color_t*)malloc((size_t)sw_ * sh_ * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, (uint32_t)sw_ * sh_);

    static lv_disp_drv_t disp;
    lv_disp_drv_init(&disp);
    disp.hor_res  = sw_;
    disp.ver_res  = sh_;
    disp.flush_cb = flushCb;
    disp.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp);

    // LVGL 键盘 indev(游戏主体走 engine::input, 这里仅供 LVGL 控件)
    static lv_indev_drv_t indev;
    lv_indev_drv_init(&indev);
    indev.type = LV_INDEV_TYPE_KEYPAD;
    indev.read_cb = indevReadCb;
    lv_indev_drv_register(&indev);

    // 引擎: 素材 + 显示(canvas) + 音频(Qt Multimedia)
    static DiskFs fs;

    if (pkgPath != nullptr) {
        // 1) 脚本游戏: 参数本身就是 .gbn, 它同时也是素材来源
        if (!pkg_.load_from(fs, pkgPath)) {
            fprintf(stderr, "[sim] .gbn 解析失败: %s\n", pkgPath);
            fflush(stderr);
        }
        engine_.assets.set_fs(&pkg_);
        engine_.load_assets("/");
    } else {
        // 2) 卡带: <工程根>/games-bin/<game>.gbn —— C++ 游戏也用 .gbn 装素材, 容器格式统一
        //    注意①: 必须用**本次要跑的那个游戏名**, 不能用 default_game_name(),
        //           否则 fun_sim fighter 会拿到 skyraider 的卡带(素材全部查不到 -> 黑屏)。
        //    注意②: 路径要解析成**绝对路径**。双击 exe 或从别的目录启动时, 工作目录不是
        //           工程根目录, 相对路径 "games-bin/xxx.gbn" 就找不到; 旧代码会静默往下
        //           回落到内嵌表(那是 skyraider 的图), 于是游戏只剩程序画的背景、人物全没
        //           —— 看起来像渲染坏了。现在改成: 找不到就明确报错 + 探针兜底。
        const bool force_disk = (getenv("FUNSIM_ASSETS") != nullptr);
        const std::string cart = find_cart(game_id_.c_str());
        std::string dir = find_assets_dir();      // 默认 <工程根>/assets-packed
        if (dir.empty() && assetsDir != nullptr && QFileInfo(QString::fromUtf8(assetsDir)).isDir()) {
            dir = assetsDir;                      // 命令行显式给的素材目录
        }

        if (!force_disk && !cart.empty() && pkg_.load_from(fs, cart)) {
            engine_.assets.set_fs(&pkg_);
            engine_.load_assets("/");
            fprintf(stderr, "[sim] 卡带: %s (%u 字节)\n", cart.c_str(), (unsigned)pkg_.size());
        } else if (!dir.empty()) {
            // 3) 开发期兜底: 直接读 assets-packed/ 目录(改素材免打包)
            //    设 FUNSIM_ASSETS=disk 可强制走这一条
            engine_.assets.set_fs(&fs);
            engine_.load_assets(dir.c_str());
            fprintf(stderr, "[sim] 素材目录: %s%s\n", dir.c_str(),
                    cart.empty() ? "  (没找到卡带)" : "");
        } else if (engine::rom_fs().available()) {
            // 4) 内嵌表(编译期由 tools/embed_assets.py 烧进 exe)。只在卡带和素材目录都
            //    没有时才用 —— 它很可能根本没包含本游戏的图, 所以下面统一过探针。
            engine_.assets.set_fs(&engine::rom_fs());
            engine_.load_assets("");
            fprintf(stderr, "[sim] 卡带: 内嵌表(%d 个素材); 磁盘上没有 games-bin/%s.gbn\n",
                    engine::rom_fs().count(), game_id_.c_str());
        } else {
            fprintf(stderr,
                    "[sim] 找不到任何素材源: games-bin/%s.gbn / assets-packed/ / 内嵌表 全都没有\n",
                    game_id_.c_str());
        }
        fflush(stderr);
    }
    engine_.display.init();
    engine_.audio.attach(&audio_);
    engine_.audio.start();

    // 素材探针: 素材源里连本游戏的第一张图都没有 —— 现在就说清楚, 别等
    // "怎么只有背景没有人物"再去查。固件那边 game_launch() 用的是同一招。
    const char* probe = games::game_probe_asset(game_id_.c_str());
    if (probe != nullptr && engine_.img(probe) == nullptr) {
        char buf[160];
        snprintf(buf, sizeof(buf), "找不到探针贴图 \"%s\" (%s 的素材源没对上)",
                 probe, game_id_.c_str());
        assets_note_ = buf;
        fprintf(stderr,
                "[sim] !! 素材缺失: %s\n"
                "     游戏只会画出程序生成的背景, 人物/道具全都不出现 —— 不是渲染坏了。\n"
                "     应读 games-bin/%s.gbn; 若从别的目录启动请设 FUNSIM_CART_DIR 指定卡带目录。\n",
                buf, game_id_.c_str());
        fflush(stderr);
    }
}

uint32_t GameWindow::lvKey() const {
    if (held_ & (1u << (uint8_t)engine::Button::Up))    return LV_KEY_UP;
    if (held_ & (1u << (uint8_t)engine::Button::Down))  return LV_KEY_DOWN;
    if (held_ & (1u << (uint8_t)engine::Button::Left))  return LV_KEY_LEFT;
    if (held_ & (1u << (uint8_t)engine::Button::Right)) return LV_KEY_RIGHT;
    if (held_ & (1u << (uint8_t)engine::Button::A))     return LV_KEY_ENTER;
    if (held_ & (1u << (uint8_t)engine::Button::B))     return LV_KEY_ESC;
    return 0;
}

void GameWindow::tick() {
    engine_.input.held     = held_;
    engine_.input.pressed  = pressed_;
    engine_.input.released = released_;
    pressed_ = released_ = 0;

    lv_timer_handler();

    float dt = (float)clock_.restart() / 1000.0f;
    if (dt > 0.25f) dt = 0.25f;
    if (game_) engine_.frame(*game_, dt);

    lv_tick_inc((uint32_t)(dt * 1000.0f) + 1);
    update();
}

void GameWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);   // 最近邻(像素风)
    p.drawImage(QRect(0, 0, sw_ * scale_, sh_ * scale_), fb_);

    // 平台分辨率比游戏逻辑画布(428x142)大时, 把游戏实际占用的区域框出来:
    // 一眼就能看出"这个游戏在这块屏上只用了左上角"还是"填满了"。
    constexpr int kGameW = 428, kGameH = 142;
    if (sw_ > kGameW || sh_ > kGameH) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(60, 200, 255, 140), 1, Qt::DashLine));
        p.drawRect(QRect(0, 0, qMin(sw_, kGameW) * scale_ - 1,
                                qMin(sh_, kGameH) * scale_ - 1));
    }

    // 素材没配对上就压一条红横幅: 否则画面看起来就是"背景正常、人物凭空消失",
    // 极容易被当成渲染 bug 查半天。
    if (!assets_note_.empty()) {
        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        const QString txt = QStringLiteral("!! 素材缺失: %1 —— 详见控制台/启动器状态栏")
                                .arg(QString::fromUtf8(assets_note_.c_str()));
        const QFontMetrics fm(f);
        const int th = fm.height() + 6;
        const int y  = height() - th - 3;
        p.fillRect(QRect(0, y, width(), th), QColor(150, 24, 24, 230));
        p.setPen(QColor(255, 240, 240));
        p.drawText(QRect(6, y, width() - 12, th), Qt::AlignVCenter | Qt::AlignLeft, txt);
    }
}

bool GameWindow::keyToButton(int qtKey, engine::Button& out) {
    switch (qtKey) {
        // 方向键 = 十字键
        case Qt::Key_Up:        out = engine::Button::Up;     return true;
        case Qt::Key_Down:      out = engine::Button::Down;   return true;
        case Qt::Key_Left:      out = engine::Button::Left;   return true;
        case Qt::Key_Right:     out = engine::Button::Right;  return true;
        // 字母键直接对应同名手柄键(A/B/C/D/X/Y)
        case Qt::Key_A:         out = engine::Button::A;      return true;
        case Qt::Key_B:         out = engine::Button::B;      return true;
        case Qt::Key_C:         out = engine::Button::C;      return true;
        case Qt::Key_D:         out = engine::Button::D;      return true;
        case Qt::Key_X:         out = engine::Button::X;      return true;
        case Qt::Key_Y:         out = engine::Button::Y;      return true;
        // 肩键 / 开始 / 选择
        case Qt::Key_Q:         out = engine::Button::L;      return true;
        case Qt::Key_R:         out = engine::Button::R;      return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:     out = engine::Button::Start;  return true;
        case Qt::Key_Space:     out = engine::Button::Select; return true;
        default: return false;
    }
}

void GameWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { close(); return; }
    engine::Button b;
    if (!keyToButton(e->key(), b)) { QWidget::keyPressEvent(e); return; }
    const uint16_t m = (uint16_t)(1u << (uint8_t)b);
    held_ |= m;
    if (!e->isAutoRepeat()) {
        pressed_ |= m;
        if (keylog_enabled()) {
            fprintf(stderr, "[sim] key DOWN vk=%d btn=%u held=0x%04X\n",
                    e->key(), (unsigned)b, (unsigned)held_);
            fflush(stderr);
        }
    }
}

void GameWindow::keyReleaseEvent(QKeyEvent* e) {
    engine::Button b;
    if (!keyToButton(e->key(), b)) { QWidget::keyReleaseEvent(e); return; }
    const uint16_t m = (uint16_t)(1u << (uint8_t)b);
    held_ &= (uint16_t)~m;
    released_ |= m;
    if (!e->isAutoRepeat()) {
        released_ |= m;
        if (keylog_enabled()) {
            fprintf(stderr, "[sim] key UP   vk=%d btn=%u held=0x%04X\n",
                    e->key(), (unsigned)b, (unsigned)held_);
            fflush(stderr);
        }
    }
}

void GameWindow::focusOutEvent(QFocusEvent* e) {
    // 失焦时把按住的键全部松开, 否则切出去以后游戏会一直"按住"某个方向
    if (held_ != 0) {
        released_ |= held_;
        held_ = 0;
    }
    QWidget::focusOutEvent(e);
}

void GameWindow::closeEvent(QCloseEvent* e) {
    engine_.quit();
    audio_.stop();
    QWidget::closeEvent(e);
}

} // namespace sim

