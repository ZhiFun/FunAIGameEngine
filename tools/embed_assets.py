#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
embed_assets.py —— 把 assets-packed/ 编成 C++ 源, 让素材跟着固件一起烧进去。

这就是 FC 的 PRG/CHR ROM、GBA 的 0x08000000 ROM 的思路:
  素材是**程序镜像的一部分**, 通过地址直接访问, 不存在"文件找不到"。
(FC: 卡带 ROM 映射进 CPU 地址空间; GBA: 0x08000000 直接可读, 还带 BIOS 解压例程。
 两者的共同点是——没有文件系统, 没有文件名, 没有加载失败。)

本工程的 AssetStore 仍按名字要素材, 所以这里生成一张静态查表
  { "skyraider/player.img" -> (字节数组, 长度) }
交给 engine::RomFileSystem 提供。

用法:
    py -3 tools/embed_assets.py --src games-bin/skyraider.gbn   # 从卡带生成(推荐)
    py -3 tools/embed_assets.py                     # 或直接扫 assets-packed/
    py -3 tools/embed_assets.py --src A --out B
    py -3 tools/embed_assets.py --check             # 只检查是否过期(不写文件), 过期退出码 1
    py -3 tools/embed_assets.py --list              # 只列出会打包哪些素材
"""
import argparse
import os
import struct
import sys

DEFAULT_SRC = "assets-packed"
DEFAULT_OUT = os.path.join("engine", "src", "engine", "rom_assets_data.h")
# 只打包引擎认识的两种格式(见 docs/ASSETS.md); 其他扩展名一律跳过
KNOWN_EXT = (".img", ".snd")


def collect_dir(src):
    """目录模式: 返回 [(名字, 字节数, 数据)], 按名字排序保证可复现。"""
    items = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for fn in sorted(files):
            if not fn.lower().endswith(KNOWN_EXT):
                continue
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            with open(full, "rb") as f:
                items.append((rel, os.path.getsize(full), f.read()))
    items.sort(key=lambda t: t[0])
    return items


def parse_gbn(path):
    """卡带模式: 直接从 .gbn 里读出素材表。

    这样 .gbn 就是唯一的打包/分发单位 —— 素材先打成卡带, 再把卡带内嵌进固件,
    两边看到的是同一份数据。格式见 docs/SCRIPT.md。
    """
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"GBN1":
        raise SystemExit("不是 .gbn 文件(魔数不对): %s" % path)
    try:
        pos = 4
        _version, _globals = struct.unpack_from("<HH", blob, pos)
        pos += 4
        pos += 16                                     # init/start/update/render 入口
        code_size, str_count, asset_count = struct.unpack_from("<III", blob, pos)
        pos += 12
        pos += code_size                              # 字节码段(纯素材卡带为 0)
        for _ in range(str_count):                    # 字符串表
            n = struct.unpack_from("<H", blob, pos)[0]
            pos += 2 + n
        items = []
        for _ in range(asset_count):                  # 素材表
            n = struct.unpack_from("<H", blob, pos)[0]
            pos += 2
            name = blob[pos:pos + n].decode("utf-8")
            pos += n
            size = struct.unpack_from("<I", blob, pos)[0]
            pos += 4
            items.append((name, size, blob[pos:pos + size]))
            pos += size
    except struct.error:
        raise SystemExit("解析 .gbn 失败(文件被截断?): %s" % path)
    return items


def collect(src):
    """src 可以是素材目录, 也可以是 .gbn 卡带。"""
    if os.path.isfile(src) and src.lower().endswith(".gbn"):
        return parse_gbn(src)
    return collect_dir(src)


def c_ident(index):
    return "kA%d" % index


def emit_blob(out, index, data, indent="    "):
    """把字节数组写成 C 字符串字面量(比 {0x..,} 短约 1/3, 编译也更快)。"""
    name = c_ident(index)
    out.append("static const char %s[] =" % name)
    chunk = []
    line = indent + '"'
    for b in data:
        esc = "\\x%02x" % b
        # 每行放 32 个转义(≈128 列), 太长会让编译器/编辑器都难受
        if len(chunk) >= 32:
            out.append(line + '"')
            line = indent + '"'
            chunk = []
        line += esc
        chunk.append(esc)
    out.append(line + '";')
    out.append("")


def generate(items, out_path, label):
    if not items:
        raise SystemExit("没有素材可内嵌")

    out = []
    out.append("// ============================================================================")
    out.append("// rom_assets_data.h —— 自动生成, 不要手改!")
    out.append("//")
    out.append("//   由 tools/embed_assets.py 从 %s 生成。" % label)
    out.append("//   重新生成:  py -3 tools/embed_assets.py")
    out.append("//   或在 CMake 里: cmake --build <build-dir> --target embed-assets")
    out.append("//")
    out.append("//   这些数组会作为常量数据直接进入固件镜像(.rodata -> flash),")
    out.append("//   运行期由 engine::RomFileSystem 按名字查表返回, 不读任何文件系统。")
    out.append("// ============================================================================")
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("namespace engine {")
    out.append("namespace rom {")
    out.append("")
    out.append("struct Entry {")
    out.append("    const char*    name;   // 例如 \"skyraider/player.img\"")
    out.append("    const uint8_t* data;")
    out.append("    uint32_t       size;")
    out.append("};")
    out.append("")

    total = 0
    for i, (rel, size, data) in enumerate(items):
        total += len(data)
        out.append("// %s (%d 字节)" % (rel, len(data)))
        emit_blob(out, i, data)

    out.append("static const Entry kTable[] = {")
    for i, (rel, size, data) in enumerate(items):
        out.append('    { "%s", (const uint8_t*)%s, %d },' % (rel, c_ident(i), size))
    out.append("};")
    out.append("static const uint32_t kCount = %d;" % len(items))
    out.append("")
    out.append("} // namespace rom")
    out.append("} // namespace engine")
    out.append("")

    text = "\n".join(out)
    changed = True
    if os.path.exists(out_path):
        with open(out_path, "r", encoding="utf-8") as f:
            changed = (f.read() != text)
    if changed:
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return items, total, changed


def fingerprint(src):
    """源素材/卡带的指纹(名字+大小+修改时间), 用来判断生成的文件是否过期。"""
    if os.path.isfile(src):
        st = os.stat(src)
        return "%s:%d:%d" % (os.path.basename(src), st.st_size, int(st.st_mtime))
    h = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for fn in sorted(files):
            full = os.path.join(root, fn)
            h.append("%s:%d:%d" % (os.path.relpath(full, src).replace(os.sep, "/"),
                                   os.path.getsize(full), int(os.path.getmtime(full))))
    return "\n".join(h)


def main():
    ap = argparse.ArgumentParser(description="把 assets-packed/ 编成 C++ 内嵌素材表")
    ap.add_argument("--src", default=DEFAULT_SRC,
                    help="素材目录或 .gbn 卡带(默认 %s)" % DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT, help="输出头文件(默认 %s)" % DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="只检查是否过期, 不写文件")
    ap.add_argument("--list", action="store_true", help="只列出会打包的素材")
    args = ap.parse_args()

    if not os.path.exists(args.src):
        raise SystemExit("素材来源不存在: %s" % args.src)

    items = collect(args.src)
    if args.list:
        for rel, size, _data in items:
            print("%-52s %8d" % (rel, size))
        print("共 %d 个, %.1f KB" % (len(items), sum(i[1] for i in items) / 1024.0))
        return 0

    stamp = fingerprint(args.src)
    stamp_path = args.out + ".stamp"

    if args.check:
        fresh = os.path.exists(args.out) and os.path.exists(stamp_path)
        if fresh:
            with open(stamp_path, "r", encoding="utf-8") as f:
                fresh = (f.read() == stamp)
        if fresh:
            print("内嵌素材表是最新的")
            return 0
        print("内嵌素材表已过期, 需要重新生成: py -3 tools/embed_assets.py")
        return 1

    items, total, changed = generate(items, args.out, args.src)
    with open(stamp_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(stamp)
    print("%s: %d 个素材, %.1f KB%s" % (
        args.out, len(items), total / 1024.0, (" (已更新)" if changed else " (内容未变)")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
