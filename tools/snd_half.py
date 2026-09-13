#!/usr/bin/env python3
"""【维修工具】把已经存在的 .snd 文件按采样率上限过一遍。

  py -3 tools\\snd_half.py games-src\\fighter games-src\\skyraider games-src\\keychase
  py -3 tools\\snd_half.py --check games-src\\fighter      # 只看现状不改
  py -3 tools\\snd_half.py --rate 8000 some_dir           # 指定上限

⚠ 正常情况下**不需要**手动跑这个 —— 打包链路已经强制执行同一套策略:
  * `pack_assets.pack_wav()` 生成 SND1 时就按 `SND_RATE` 输出(源头就是瘦的)
  * `build_game.py` 会把任何进卡带的 .snd 再过一道 `sndlib.limit()`
这个脚本只在"仓库里已经堆了一堆超标素材"时用来一次性修干净(比如刚导入别人的素材)。
策略本身见 tools/sndlib.py。

已经 <= 上限的文件会跳过, 所以可以重复跑。
"""
import argparse
import glob
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sndlib                            # noqa: E402


def main():
    ap = argparse.ArgumentParser(prog="snd_half", description="SND1 采样率上限维修工具")
    ap.add_argument("dirs", nargs="+", help="含 .snd 的目录")
    ap.add_argument("--check", action="store_true", help="只报告不改文件")
    ap.add_argument("--rate", type=int, default=sndlib.MAX_RATE,
                    help=f"采样率上限(默认 {sndlib.MAX_RATE}Hz)")
    a = ap.parse_args()

    total_before = total_after = fixed = 0
    for d in a.dirs:
        files = sorted(glob.glob(os.path.join(d, "*.snd")))
        if not files:
            continue
        print(f"=== {d} ({len(files)} 个) ===")
        for p in files:
            raw = open(p, "rb").read()
            before = len(raw)
            new, note = sndlib.limit(raw, a.rate)
            after = len(new)
            total_before += before
            total_after += after
            if after != before:
                fixed += 1
                if not a.check:
                    open(p, "wb").write(new)
                print(f"   {os.path.basename(p):<22} {note:<18} {before}->{after} 字节")
            elif a.check:
                print(f"   {os.path.basename(p):<22} {note}")
    print(f"{'可降' if a.check else '已降'} {fixed} 个, "
          f"合计 {total_before} -> {total_after} 字节(省 {total_before - total_after})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
