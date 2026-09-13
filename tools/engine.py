#!/usr/bin/env python3
"""FunAIGameEngine 命令行工具。

  py -3 tools\\engine.py gen-samples      # 生成示例素材
  py -3 tools\\engine.py assets           # 打包 assets-src -> assets-packed
  py -3 tools\engine.py configure --qt-dir C:/Qt/6.9.3/mingw_64
  py -3 tools\\engine.py build            # 编译模拟器
  py -3 tools\\engine.py run [game]       # 运行模拟器
  py -3 tools\\engine.py new mygame       # 从模板新建游戏(C++ 方式)
  py -3 tools\\engine.py pack starfall    # 脚本+素材 -> games-bin/starfall.gbn
  py -3 tools\\engine.py run games-bin\\starfall.gbn   # 模拟器直接跑 .gbn
  py -3 tools\\engine.py export mygame    # 导出到默认硬件实例(esp32s3keyboard)
  py -3 tools\\engine.py export mygame --target esp32s3   # 指定其它实例
  py -3 tools\\engine.py targets          # 列出可用硬件实例
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 默认硬件实例: FunModularKeyboard(ESP32-S3 + NV3007 428x142)。
# 想换回通用 ESP32-S3 模板用 --target esp32s3。
DEFAULT_TARGET = "esp32s3keyboard"

GAME_TEMPLATE = '''// ============================================================================
// @NAME@ —— 引擎示例(由 tools/engine.py new 生成)
// 参照 docs/AI_GAMEDEV_GUIDE.md 填写玩法。
// ============================================================================
#include "games/@NAME@/game.h"
#include "engine/Engine.h"

using namespace engine;

namespace {

class @NAME@Game : public Game {
    float x = 40, y = 40;
public:
    const char* name() const override { return "@NAME@"; }

    void on_start(Engine& e) override {
        // e.audio.play_bgm(e.snd("@NAME@/bgm"), 0.8f);
    }

    void on_update(Engine& e, float dt) override {
        const float spd = 140.0f * dt;
        if (is_held(e.input, Button::Left))  x -= spd;
        if (is_held(e.input, Button::Right)) x += spd;
        if (is_held(e.input, Button::Up))    y -= spd;
        if (is_held(e.input, Button::Down))  y += spd;
        if (is_pressed(e.input, Button::A)) {
            // e.audio.play_sfx(e.snd("@NAME@/sfx_click"));
        }
    }

    void on_render(Engine& e) override {
        Display& d = e.display;
        d.clear(rgb565(8, 8, 24));
        d.text(8, 8, "@NAME@", rgb565(255, 255, 255), 2);
        d.fill_rect((int)x, (int)y, 12, 12, rgb565(120, 220, 255));
        d.text(8, 300, "ARROWS=MOVE  A=?", rgb565(150, 170, 200), 1);
    }
};

} // namespace

namespace games {
engine::Game* make_@NAME@() { return new @NAME@Game(); }
}
'''


def run(cmd, cwd):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], cwd=str(cwd), check=True)


def cmd_gen_samples(_):
    run([sys.executable, str(ROOT / "tools" / "gen_sample_assets.py")], ROOT)


def cmd_assets(_):
    run([sys.executable, str(ROOT / "tools" / "pack_assets.py")], ROOT)


def cmd_pack(a):
    """脚本游戏 -> 单个 .gbn 文件(键盘固件/模拟器直接加载)。"""
    cmd = [sys.executable, str(ROOT / "tools" / "build_game.py")]
    if a.all:
        cmd.append("--all")
    elif a.name:
        cmd.append(a.name)
    else:
        print("需要游戏名, 或用 --all")
        return 1
    cmd += ["--out-dir", a.out_dir]
    run(cmd, ROOT)
    return 0


def cmd_configure(a):
    build = Path(a.build)
    cmd = ["cmake", "-S", str(ROOT), "-B", str(build)]
    if a.qt_dir:
        cmd += ["-DCMAKE_PREFIX_PATH=" + a.qt_dir]
    run(cmd, ROOT)


def _find_exe(build):
    for p in Path(build).rglob("fun_sim.exe"):
        return p
    return None


def cmd_build(a):
    cmd_assets(a)
    run(["cmake", "--build", str(a.build), "--config", "Release"], ROOT)


def cmd_run(a):
    cmd_assets(a)
    exe = _find_exe(a.build)
    if exe is None:
        print(f"没找到 fun_sim.exe(先在 {a.build} 里 configure + build)")
        return 1
    run([str(exe), a.game, str(ROOT / "assets-packed")], ROOT)


def cmd_new(a):
    name = a.name.strip().lower()
    if not name.replace("_", "").isalnum():
        print("名字只能用字母/数字/下划线")
        return 1
    d = ROOT / "games" / name
    d.mkdir(parents=True, exist_ok=True)
    header = ('#pragma once\n'
              '#include "engine/Game.h"\n'
              f'namespace games {{ engine::Game* make_{name}(); }}\n')
    (d / "game.h").write_text(header, encoding="utf-8")
    (d / "game.cpp").write_text(GAME_TEMPLATE.replace("@NAME@", name), encoding="utf-8")
    (ROOT / "assets-src" / name).mkdir(parents=True, exist_ok=True)
    print(f"created games/{name}/game.h, games/{name}/game.cpp, assets-src/{name}/")
    print("下一步:")
    print(f'  1) 在 games/registry.cpp 加一行:  if (strcmp(name,"{name}")==0) return make_{name}();')
    print("  2) 素材放 assets-src/ 里, 再跑:  py -3 tools\\engine.py assets")


def _copy_tree(src: Path, dst: Path):
    for f in src.rglob("*"):
        if f.is_file():
            rel = f.relative_to(src)
            t = dst / rel
            t.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, t)


def _available_targets():
    pdir = ROOT / "platform"
    if not pdir.is_dir():
        return []
    return sorted(p.name for p in pdir.iterdir() if p.is_dir())


def cmd_targets(_):
    for t in _available_targets():
        mark = "  (默认)" if t == DEFAULT_TARGET else ""
        print(f"  {t}{mark}")
    return 0


def cmd_export(a):
    name = a.name.strip().lower()
    target = a.target
    pdir = ROOT / "platform" / target
    if not pdir.is_dir():
        print(f"没有这个硬件实例: {pdir}")
        print("可用实例: " + ", ".join(_available_targets()))
        return 1

    out = ROOT / "out" / f"{name}-{target}"
    if out.exists():
        shutil.rmtree(out)
    (out / "src").mkdir(parents=True, exist_ok=True)
    (out / "include").mkdir(parents=True, exist_ok=True)
    (out / "data").mkdir(parents=True, exist_ok=True)

    # 平台模板(硬件实例)
    _copy_tree(pdir / "src", out / "src")
    if (pdir / "include").is_dir():
        _copy_tree(pdir / "include", out / "include")
    shutil.copy2(pdir / "platformio.ini", out / "platformio.ini")
    for extra in pdir.glob("*.csv"):          # 分区表等
        shutil.copy2(extra, out / extra.name)

    # 引擎(头 -> include/engine, 源 -> src/)
    _copy_tree(ROOT / "engine" / "include", out / "include")
    for f in (ROOT / "engine" / "src").glob("*.cpp"):
        shutil.copy2(f, out / "src")

    # 游戏(注册表 + 指定游戏)
    shutil.copy2(ROOT / "games" / "registry.cpp", out / "src")
    (out / "include" / "games").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "games" / "registry.h", out / "include" / "games")
    gsrc = ROOT / "games" / name
    if not gsrc.is_dir():
        print(f"没有这个游戏: {gsrc}")
        return 1
    for f in gsrc.glob("*.cpp"):
        shutil.copy2(f, out / "src")
    hdir = out / "include" / "games" / name
    hdir.mkdir(parents=True, exist_ok=True)
    for f in gsrc.glob("*.h"):
        shutil.copy2(f, hdir)

    # 素材 -> data/(SPIFFS 是平铺文件系统, 把 skyraider/player.img 拍平成 skyraider_player.img)
    packed = ROOT / "assets-packed"
    if packed.is_dir():
        for f in packed.rglob("*"):
            if f.is_file():
                flat = str(f.relative_to(packed)).replace("\\", "/").replace("/", "_")
                shutil.copy2(f, out / "data" / flat)

    print(f"exported -> {out}   (硬件实例: {target})")
    print("  cd out\\" + f"{name}-{target}")
    print("  pio run -t uploadfs -t upload    # 先烧素材文件系统, 再烧固件")
    if target == "esp32s3keyboard":
        print("  已按 FunModularKeyboard 硬件预配置: NV3007 428x142 / I2S1 16-39-38")
        print("  (改引脚看 include/nv3007_setup.h 与 src/esp_main.cpp 顶部的矩阵表)")
    else:
        print("  (按你的板子改 src/esp_display.cpp 的 SPI 引脚 与 esp_audio.cpp 的 I2S 引脚)")
    return 0


def main():
    p = argparse.ArgumentParser(prog="engine", description="FunAIGameEngine CLI")
    s = p.add_subparsers(dest="cmd", required=True)

    s.add_parser("gen-samples", help="生成示例素材").set_defaults(fn=cmd_gen_samples)
    s.add_parser("assets", help="打包 assets-src -> assets-packed").set_defaults(fn=cmd_assets)

    k = s.add_parser("pack", help="脚本+素材 -> .gbn 游戏文件")
    k.add_argument("name", nargs="?", help="games-src 下的游戏名")
    k.add_argument("--all", action="store_true", help="打包 games-src 下全部")
    k.add_argument("--out-dir", default="games-bin")
    k.set_defaults(fn=cmd_pack)

    c = s.add_parser("configure", help="生成 CMake 工程")
    c.add_argument("--build", default="build")
    c.add_argument("--qt-dir", help="Qt 6 路径, 如 C:/Qt/6.9.3/mingw_64 (设 CMAKE_PREFIX_PATH)")
    c.set_defaults(fn=cmd_configure)

    b = s.add_parser("build", help="编译模拟器")
    b.add_argument("--build", default="build")
    b.set_defaults(fn=cmd_build)

    r = s.add_parser("run", help="运行模拟器")
    r.add_argument("game", nargs="?", default="skyraider")
    r.add_argument("--build", default="build")
    r.set_defaults(fn=cmd_run)

    n = s.add_parser("new", help="新建游戏模板")
    n.add_argument("name")
    n.set_defaults(fn=cmd_new)

    x = s.add_parser("export", help="导出 PlatformIO 硬件工程")
    x.add_argument("name")
    x.add_argument("--target", default=DEFAULT_TARGET,
                   help=f"硬件实例目录名(platform/<target>), 默认 {DEFAULT_TARGET}")
    x.set_defaults(fn=cmd_export)

    s.add_parser("targets", help="列出可用硬件实例").set_defaults(fn=cmd_targets)

    a = p.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main() or 0)
