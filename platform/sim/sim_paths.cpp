#include "sim_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstdlib>

namespace sim {
namespace {

// 从 start 开始往上最多 6 层, 找到含 marker 的那一层就返回它的绝对路径
std::string search_marker(const QString& start, const QString& marker) {
    QDir d(start);
    if (!d.exists()) return std::string();
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(d.absoluteFilePath(marker))) return d.absolutePath().toStdString();
        if (!d.cdUp()) break;
    }
    return std::string();
}

// 起始搜索点: exe 目录优先(启动器是子进程重开自己, 工作目录不一定是原来的)
QStringList start_points() {
    QStringList out;
    out << QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    if (!out.contains(cwd)) out << cwd;
    return out;
}

std::string env_path(const char* var) {
    const char* v = getenv(var);
    if (v == nullptr || v[0] == '\0') return std::string();
    return v;
}

} // namespace

std::string project_root() {
    for (const QString& s : start_points()) {
        const std::string r = search_marker(s, QStringLiteral("games-bin"));
        if (!r.empty()) return r;
    }
    for (const QString& s : start_points()) {
        const std::string r = search_marker(s, QStringLiteral("assets-packed"));
        if (!r.empty()) return r;
    }
    return std::string();
}

std::string find_cart(const char* game_id) {
    if (game_id == nullptr || game_id[0] == '\0') return std::string();
    const QString file = QStringLiteral("%1.gbn").arg(QString::fromUtf8(game_id));

    const std::string env = env_path("FUNSIM_CART_DIR");   // 显式指定优先
    if (!env.empty()) {
        const QString p = QDir(QString::fromStdString(env)).absoluteFilePath(file);
        if (QFileInfo::exists(p)) return p.toStdString();
    }
    const std::string root = project_root();
    if (!root.empty()) {
        const QString p =
            QDir(QString::fromStdString(root)).absoluteFilePath(QStringLiteral("games-bin/") + file);
        if (QFileInfo::exists(p)) return p.toStdString();
    }
    // 兜底: 相对当前工作目录(老行为)
    if (QFileInfo::exists(QStringLiteral("games-bin/") + file)) {
        return QFileInfo(QStringLiteral("games-bin/") + file).absoluteFilePath().toStdString();
    }
    return std::string();
}

std::string find_assets_dir() {
    const std::string env = env_path("FUNSIM_ASSETS_DIR");
    if (!env.empty() && QFileInfo(env.c_str()).isDir()) return env;

    const std::string root = project_root();
    if (!root.empty()) {
        const QFileInfo fi(QDir(QString::fromStdString(root)).absoluteFilePath(QStringLiteral("assets-packed")));
        if (fi.isDir()) return fi.absoluteFilePath().toStdString();
    }
    const QFileInfo rel(QStringLiteral("assets-packed"));
    if (rel.isDir()) return rel.absoluteFilePath().toStdString();
    return std::string();
}

} // namespace sim
