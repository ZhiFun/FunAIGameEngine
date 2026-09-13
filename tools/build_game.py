#!/usr/bin/env python3
"""把 games-src/<name>/ 编译打包成单个 .gbn 游戏文件。

  py -3 tools\\build_game.py starfall
  py -3 tools\\build_game.py starfall --out-dir games-bin

游戏目录结构:

  games-src/starfall/
    game.gs       脚本(见 docs/SCRIPT.md)
    star.png      素材(可选, 会写成 star.img)
    coin.wav      音效(可选, 会写成 coin.snd)

产物 games-bin/starfall.gbn = 字节码 + 全部素材, 键盘固件/模拟器直接加载它。
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import pack_assets                      # noqa: E402  复用 PNG/WAV -> .img/.snd 打包
import gs_compiler                      # noqa: E402
import sndlib                           # noqa: E402  音频采样率策略(见 sndlib.py)

MAGIC = b"GBN1"
VERSION = 1
NONE = 0xFFFFFFFF


class _Sink:
    """让 pack_assets 的输出函数写进内存而不是磁盘。"""
    def __init__(self):
        self.data = b""

    def write_bytes(self, data):
        self.data = bytes(data)


def pack_image(src: Path) -> bytes:
    sink = _Sink()
    pack_assets.pack_png(src, sink)
    return sink.data


def pack_sound(src: Path) -> bytes:
    """WAV -> SND1。pack_wav 已经按 sndlib.MAX_RATE 输出, 这里再过一道兜底。"""
    sink = _Sink()
    pack_assets.pack_wav(src, sink)
    return sndlib.limit(sink.data)[0]


def print_audio(assets):
    """把音频采样率打出来。这个策略直接决定卡带能不能装进 SPIFFS, 必须看得见。"""
    note, over = sndlib.summary([d for n, d in assets if n.lower().endswith(".snd")])
    if not note:
        return
    warn = f"  !! 有 {over} 个超过 {sndlib.MAX_RATE}Hz, 会白占 SPIFFS" if over else ""
    print(f"  {note}{warn}")


def build(name: str, out_dir: Path, quiet: bool = False, snd_rate: int = sndlib.MAX_RATE):
    gdir = ROOT / "games-src" / name
    if not gdir.is_dir():
        print(f"没有这个游戏目录: {gdir}")
        return 1
    script = gdir / "game.gs"
    if not script.is_file():
        print(f"缺少脚本: {script}")
        return 1

    try:
        code, entries, strings, _globals = gs_compiler.compile_source(
            script.read_text(encoding="utf-8"))
    except gs_compiler.CompileError as exc:
        print(f"编译失败: {exc}")
        return 1

    # ---- 素材 ----
    # 两条不变量都在这里强制(以前全凭人记得, 忘了就是卡带悄悄变胖):
    #   1. **只打包脚本真正引用过的素材**。脚本里所有素材名都是字面量(img()/sfx()/bgm()/
    #      strs 块), 所以编译出的字符串表就是完整的引用集合 —— assets-packed/<game>/ 里
    #      的历史残留(改名前的旧图、别处拷来的多余素材)不会再被顺手带进卡带。
    #   2. **同名冲突只留一份**。games-src/<game>/ 的约定是"源(.png/.wav) + 已打包(.img/.snd)"
    #      混放; 手滑把 assets-packed 的 .img 再拷进来就会同名两份, 卡带直接翻倍。
    #      源文件优先, 打包好的当兜底。
    referenced = set(strings)
    PRIO = {".png": 0, ".wav": 0, ".img": 1, ".snd": 1}
    picked, skipped, dup = {}, [], []
    for p in sorted(gdir.iterdir()):
        suffix = p.suffix.lower()
        if not p.is_file() or suffix not in PRIO:
            continue                                   # game.gs / 其他文件不管
        out_name = p.stem + (".img" if suffix in (".png", ".img") else ".snd")
        if p.stem not in referenced and out_name not in referenced:
            skipped.append(p.name)
            continue
        cur = picked.get(out_name)
        if cur is None or PRIO[suffix] < PRIO[cur.suffix.lower()]:
            if cur is not None:
                dup.append(cur.name)
            picked[out_name] = p
        else:
            dup.append(p.name)

    assets = []
    for out_name in sorted(picked):
        p = picked[out_name]
        low = p.name.lower()
        if low.endswith(".png"):
            assets.append((out_name, pack_image(p)))
        elif low.endswith(".wav"):
            assets.append((out_name, pack_sound(p)))
        elif low.endswith(".snd"):
            # 采样率必须过一道, 否则手工放一个 22050 的素材进来卡带就胖回去了
            # (幂等, 不会越降越低)
            assets.append((out_name, sndlib.limit(p.read_bytes(), snd_rate)[0]))
        else:
            assets.append((out_name, p.read_bytes()))
    # 素材过多会让 u16 名字表溢出, 实际用不到这么多
    if len(assets) > 512:
        print(f"素材太多: {len(assets)}")
        return 1

    # ---- 组装 .gbn ----
    blob = bytearray()
    blob += MAGIC
    blob += struct.pack("<HH", VERSION, len(_globals))
    blob += struct.pack("<IIII", entries.get("init", NONE), entries.get("start", NONE),
                        entries.get("update", NONE), entries.get("render", NONE))
    blob += struct.pack("<III", len(code), len(strings), len(assets))
    blob += code
    for s in strings:
        raw = s.encode("utf-8")
        if len(raw) > 0xFFFF:
            print(f"字符串过长: {s!r}")
            return 1
        blob += struct.pack("<H", len(raw)) + raw
    for aname, adata in assets:
        raw = aname.encode("utf-8")
        blob += struct.pack("<H", len(raw)) + raw + struct.pack("<I", len(adata)) + adata

    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{name}.gbn"
    out.write_bytes(blob)

    if not quiet:
        asset_kb = sum(len(d) for _, d in assets) / 1024.0
        print(f"{name}.gbn  {len(blob)} 字节 "
              f"(字节码 {len(code)}B, 字符串 {len(strings)}, 素材 {len(assets)} 个/{asset_kb:.1f}KB)")
        print_audio(assets)
        if skipped:
            head = ", ".join(skipped[:6]) + (" 等" if len(skipped) > 6 else "")
            print(f"  !! 脚本没引用, 已跳过 {len(skipped)} 个: {head}")
        if dup:
            head = ", ".join(dup[:6]) + (" 等" if len(dup) > 6 else "")
            print(f"  !! 同名两份, 已忽略 {len(dup)} 个: {head}")
        if assets:
            print("  素材: " + ", ".join(a for a, _ in assets))
        print(f"-> {out}")
    return 0


def build_cart(name: str, out_dir: Path, quiet: bool = False, snd_rate: int = sndlib.MAX_RATE):
    """把 assets-packed/<name>/ (+ common/) 打成纯素材卡带 <name>.gbn。

    C++ 写的游戏没有字节码, 但用**同一个 .gbn 容器**装素材, 好处是:

      * 卡带是唯一的打包/分发单位 —— 模拟器直接读这个文件, 固件把同一份内容
        内嵌进去(tools/embed_assets.py --src <cart>.gbn), 两边数据必然一致;
      * 素材名与脚本游戏一致(打平后的 basename), AssetStore 不用区分游戏是
        C++ 还是脚本写的。

    格式与脚本卡带完全一致, 只是 codeSize=0、无字符串表:
      GBN1 | version u16 | globalCount=0 | init/start/update/render=0xFFFFFFFF
           | codeSize=0 | stringCount=0 | assetCount | [nameLen u16|name|size u32|data]*
    """
    src = ROOT / "assets-packed" / name
    if not src.is_dir():
        print(f"没有这个素材目录: {src}")
        return 1

    # 只用游戏自己的素材目录。common/ 是跨游戏共用集, 默认不进卡带 ——
    # 卡带最终要放进设备的 SPIFFS 分区(总共才 896KB), 越小越好。
    picked = {}
    for d in (src,):
        if not d.is_dir():
            continue
        for p in sorted(d.iterdir()):
            if p.is_file() and p.name.lower().endswith(".img"):
                picked[p.name] = p.read_bytes()
            elif p.is_file() and p.name.lower().endswith(".snd"):
                picked[p.name] = sndlib.limit(p.read_bytes(), snd_rate)[0]
    if not picked:
        print(f"{src} 里没有 .img/.snd 素材")
        return 1

    assets = sorted(picked.items())

    # GamePackage::parse 要求 codeSize > 0(原本是为了挡住被截断的文件), 所以纯素材
    # 卡带放一条 1 字节的 HALT 占位; 四个入口点全是 0xFFFFFFFF, 任何东西都不会去执行它。
    dummy_code = b"\x00"                                      # OP_HALT

    blob = bytearray()
    blob += MAGIC
    blob += struct.pack("<HH", VERSION, 0)                    # 没有全局变量
    blob += struct.pack("<IIII", NONE, NONE, NONE, NONE)      # 没有字节码入口
    blob += struct.pack("<III", len(dummy_code), 0, len(assets))
    blob += dummy_code
    for aname, adata in assets:
        raw = aname.encode("utf-8")
        if len(raw) > 0xFFFF:
            print(f"素材名过长: {aname!r}")
            return 1
        blob += struct.pack("<H", len(raw)) + raw + struct.pack("<I", len(adata)) + adata

    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{name}.gbn"
    out.write_bytes(blob)

    if not quiet:
        kb = sum(len(d) for _, d in assets) / 1024.0
        print(f"{name}.gbn  卡带 {len(blob)} 字节 (素材 {len(assets)} 个/{kb:.1f}KB)")
        print_audio(assets)
        print(f"-> {out}")
    return 0


def main():
    ap = argparse.ArgumentParser(prog="build_game", description="脚本+素材 -> .gbn")
    ap.add_argument("name", nargs="?", help="games-src 下的游戏名 / 卡带名")
    ap.add_argument("--all", action="store_true", help="编译 games-src 下所有游戏")
    ap.add_argument("--cart", action="store_true",
                    help="纯素材卡带模式: assets-packed/<name>/ -> <name>.gbn")
    ap.add_argument("--out-dir", default="games-bin", help="输出目录(默认 games-bin)")
    ap.add_argument("--snd-rate", type=int, default=sndlib.MAX_RATE,
                    help=f"进卡带的音频采样率上限(默认 {sndlib.MAX_RATE}, 0 = 不动)")
    a = ap.parse_args()

    out_dir = Path(a.out_dir)
    if not out_dir.is_absolute():
        out_dir = ROOT / out_dir

    if a.cart:
        # 纯素材卡带: --cart <name> 或 --cart --all
        if a.all:
            root = ROOT / "assets-packed"
            names = sorted(p.name for p in root.iterdir()
                           if p.is_dir() and p.name != "common") if root.is_dir() else []
            if not names:
                print("assets-packed 下没有素材目录")
                return 1
            rc = 0
            for n in names:
                rc |= build_cart(n, out_dir, snd_rate=a.snd_rate)
            return rc
        if not a.name:
            print("需要卡带名(如 skyraider), 或用 --all")
            return 1
        return build_cart(a.name, out_dir, snd_rate=a.snd_rate)

    if a.all:
        root = ROOT / "games-src"
        names = sorted(p.name for p in root.iterdir() if p.is_dir()) if root.is_dir() else []
        if not names:
            print("games-src 下没有游戏")
            return 1
        rc = 0
        for n in names:
            rc |= build(n, out_dir, snd_rate=a.snd_rate)
        return rc

    if not a.name:
        print("需要游戏名, 或用 --all")
        return 1
    return build(a.name, out_dir, snd_rate=a.snd_rate)


if __name__ == "__main__":
    sys.exit(main())
