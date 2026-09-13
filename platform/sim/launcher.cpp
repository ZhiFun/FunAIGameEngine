#include "launcher.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdio>

#include "games/registry.h"
#include "sim_paths.h"
#include "sim_platform.h"

namespace sim {
namespace {

// 卡带在不在? 不在的话下拉框里标出来。
// 注意用 sim::find_cart() 而不是相对路径 —— 从别的目录启动 exe 时工作目录不是
// 工程根目录, 相对路径会误报"缺卡带", 而子进程也会真的加载不到素材。
bool cart_exists(const QString& game_id) {
    return !sim::find_cart(game_id.toUtf8().constData()).empty();
}

// 脚本游戏(games-src/<name>/game.gs) 的目录, 相对于工程根目录找
QDir games_src_dir() {
    const std::string root = sim::project_root();
    return QDir(root.empty() ? QStringLiteral(".") : QString::fromStdString(root));
}

// games-src/<name>/game.gs —— 脚本游戏(带字节码的 .gbn), 直接用包路径启动
QStringList script_games() {
    QStringList out;
    QDir src(games_src_dir().absoluteFilePath(QStringLiteral("games-src")));
    if (!src.exists()) return out;
    for (const QFileInfo& d : src.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(d.absoluteFilePath() + "/game.gs")) out << d.fileName();
    }
    return out;
}

const char* kStyle = R"(
QWidget { background:#12161f; color:#dfe6f2; font-size:13px; }
QComboBox { background:#1b2230; border:1px solid #2c3648; border-radius:4px; padding:4px 8px; }
QComboBox::drop-down { border:0; width:18px; }
QComboBox QAbstractItemView { background:#1b2230; selection-background-color:#26405c; }
QPushButton { background:#22405f; border:1px solid #3a5f88; border-radius:4px; padding:7px 18px; }
QPushButton:hover { background:#2b527a; }
QPushButton:disabled { background:#1a1f29; color:#5c6b80; }
QLabel#hint { color:#8ea2bd; }
QLabel#status { color:#7fd6ff; }
)";

} // namespace

LauncherWindow::LauncherWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("FunAIGameEngine · 模拟器"));
    setStyleSheet(QString::fromUtf8(kStyle));
    child_ = new QProcess(this);
    build_ui();
    populate_games();
    QObject::connect(child_, &QProcess::readyReadStandardError, this, [this]() {
        // 把子进程的日志原样转发出来(卡带来源/分辨率这些调试信息很有用)
        const QByteArray err = child_->readAllStandardError();
        if (!err.isEmpty()) {
            fwrite(err.constData(), 1, (size_t)err.size(), stderr);
            fflush(stderr);
            // 子进程写的是 UTF-8(源码里的中文), 不能用 fromLocal8Bit 解
            status_->setText(QString::fromUtf8(err).trimmed().right(160));
        }
    });
    QObject::connect(child_, &QProcess::stateChanged, this, [this](QProcess::ProcessState s) {
        if (s == QProcess::NotRunning) status_->setText(QStringLiteral("游戏窗口已关闭"));
    });
}

void LauncherWindow::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("选择要运行的游戏和硬件平台"));
    title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold; color:#f0f6ff;"));
    root->addWidget(title);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    game_ = new QComboBox(this);
    game_->setMinimumWidth(280);
    form->addRow(QStringLiteral("游戏"), game_);

    platform_ = new QComboBox(this);
    int plat_count = 0;
    const PlatformInfo* plats = platform_list(&plat_count);
    for (int i = 0; i < plat_count; ++i) {
        platform_->addItem(QString::fromUtf8(plats[i].label), QString::fromUtf8(plats[i].id));
    }
    if (plat_count > 0) {
        scale_ = new QComboBox(this);
        const int def = plats[0].scale;
        for (int s = 1; s <= 4; ++s) {
            scale_->addItem(QStringLiteral("%1x").arg(s), s);
            if (s == def) scale_->setCurrentIndex(scale_->count() - 1);
        }
    }
    form->addRow(QStringLiteral("硬件平台"), platform_);
    if (scale_ != nullptr) form->addRow(QStringLiteral("窗口缩放"), scale_);
    root->addLayout(form);

    // 换平台时把缩放拉回该平台的默认值(不然 480x320 还按 2x 就出屏了)
    QObject::connect(platform_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (scale_ == nullptr) return;
        int n = 0;
        const PlatformInfo* list = platform_list(&n);
        if (idx < 0 || idx >= n) return;
        for (int i = 0; i < scale_->count(); ++i) {
            if (scale_->itemData(i).toInt() == list[idx].scale) {
                scale_->setCurrentIndex(i);
                break;
            }
        }
    });

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color:#2c3648;"));
    root->addWidget(line);

    auto* row = new QHBoxLayout();
    auto* start = new QPushButton(QStringLiteral("启动 / 重载"), this);
    auto* stop  = new QPushButton(QStringLiteral("关闭游戏窗口"), this);
    QObject::connect(start, &QPushButton::clicked, this, [this]() { launch(); });
    QObject::connect(stop,  &QPushButton::clicked, this, [this]() { stop_child(); });
    row->addWidget(start);
    row->addWidget(stop);
    row->addStretch(1);
    root->addLayout(row);

    status_ = new QLabel(QStringLiteral("未启动"), this);
    status_->setObjectName(QStringLiteral("status"));
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    hint->setText(QStringLiteral(
        "操作: 方向键 = 十字键 · A/B/C/D = 手柄 A/B/C/D · X/Y = X/Y · Q/R = 肩键 "
        "· 回车 = Start · 空格 = Select · ESC = 关闭游戏窗口\n"
        "选平台会以该设备的分辨率建一个独立窗口(键盘实例 428x142 / S3 开发板 480x320);"
        "平台比游戏画布大时, 窗口里用青色虚线框标出游戏实际占用的区域。\n"
        "素材来源: 卡带 games-bin/<游戏>.gbn(路径从 exe 目录往上找工程根目录解析, "
        "与启动器所在目录无关); 设 FUNSIM_ASSETS=disk 可改读 assets-packed/ 目录。"));
    root->addWidget(hint);
}

void LauncherWindow::populate_games() {
    int count = 0;
    const games::GameInfo* list = games::game_list(&count);
    for (int i = 0; i < count; ++i) {
        const QString id = QString::fromUtf8(list[i].id);
        QString text = QString::fromUtf8(list[i].label);
        if (!cart_exists(id)) text += QStringLiteral("   [缺卡带 games-bin/%1.gbn]").arg(id);
        game_->addItem(text, id);
    }
    // 脚本游戏: data 直接用包路径, 主程序按 ".gbn" 后缀认出来
    for (const QString& name : script_games()) {
        const QString path = games_src_dir().absoluteFilePath(
            QStringLiteral("games-bin/%1.gbn").arg(name));
        QString text = QStringLiteral("%1 · 脚本游戏").arg(name);
        if (!QFileInfo::exists(path)) text += QStringLiteral("   [缺 %1]").arg(path);
        game_->addItem(text, path);
    }
    if (game_->count() == 0) {
        game_->addItem(QStringLiteral("(games-bin 下没有游戏)"), QString());
    }
}

void LauncherWindow::launch() {
    stop_child();

    const QString game = game_->currentData().toString();
    if (game.isEmpty()) {
        status_->setText(QStringLiteral("没有可启动的游戏"));
        return;
    }
    const QString plat =
        platform_->currentData().isValid() ? platform_->currentData().toString()
                                           : QString::fromUtf8(default_platform()->id);
    const int scale = (scale_ != nullptr) ? scale_->currentData().toInt() : 0;

    QStringList args;
    args << game << QStringLiteral("assets-packed")
         << QStringLiteral("--device") << plat;
    if (scale > 0) args << QStringLiteral("--zoom") << QString::number(scale);

    status_->setText(QStringLiteral("启动中: %1  [%2]").arg(game, plat));
    child_->setProgram(QCoreApplication::applicationFilePath());
    child_->setArguments(args);
    child_->setProcessChannelMode(QProcess::SeparateChannels);
    // 关键: 子进程的工作目录设成工程根目录。否则双击 exe 启动时子进程会在
    // exe 边上找 games-bin/, 找不到 -> 素材全空 -> 游戏只剩背景(人物不见)。
    const std::string root = sim::project_root();
    if (!root.empty()) child_->setWorkingDirectory(QString::fromStdString(root));
    child_->start();
    if (!child_->waitForStarted(3000)) {
        status_->setText(QStringLiteral("启动失败: %1").arg(child_->errorString()));
    }
}

void LauncherWindow::stop_child() {
    if (child_->state() == QProcess::NotRunning) return;
    child_->kill();
    child_->waitForFinished(800);
}

} // namespace sim
