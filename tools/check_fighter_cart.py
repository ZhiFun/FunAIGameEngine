#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_fighter_cart.py —— 交叉核对: 游戏代码会去取的名字 vs 卡带里真实存在的素材。

教训(2026-09-13): 素材改名(动作加帧号)后如果卡带没重打包, 或者动作帧数和
games/fighter/game.cpp 里的 frame_count() 对不上, 结果就是**人物直接不画出来**
(blit 拿到 nullptr 就 return), 画面上只剩程序生成的背景 —— 极容易被当成渲染 bug。

用法: py -3 tools\\check_fighter_cart.py [games-bin\\fighter.gbn]
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 与 games/fighter/game.cpp 的 frame_count() 表一一对应(改代码请同步这里)
ACTIONS = {
    "idle": 2, "walk": 4, "crouch": 1, "jump": 2, "punch": 4, "kick": 4,
    "cpush": 2, "ckick": 3, "hurt": 2, "super": 3, "ko": 2,
}
CHARS = ["keyspr", "boxer", "ninja", "brute"]
PROPS = ["cap_small", "cap_big", "cap_tiny", "big_kb", "gun", "spark", "ring",
         "dizzy", "shadow"]


def read_cart_assets(path):
    b = Path(path).read_bytes()
    assert b[:4] == b"GBN1", "不是 GBN1 卡带"
    code_size, str_count, asset_count = struct.unpack_from("<III", b, 24)
    p = 36 + code_size
    for _ in range(str_count):
        n = struct.unpack_from("<H", b, p)[0]
        p += 2 + n
    out = []
    for _ in range(asset_count):
        n = struct.unpack_from("<H", b, p)[0]
        p += 2
        name = b[p:p + n].decode("utf-8", "replace")
        p += n
        size = struct.unpack_from("<I", b, p)[0]
        p += 4
        out.append((name, size, b[p:p + 4]))
        p += size
    return out, len(b)


def main():
    cart = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "games-bin" / "fighter.gbn"
    assets, total = read_cart_assets(cart)
    have = {n: (s, magic) for n, s, magic in assets}

    want = [f"{c}_{a}{i}" for c in CHARS for a, n in ACTIONS.items() for i in range(n)]
    want += PROPS

    missing = [w for w in want if f"{w}.img" not in have]
    extra = [n for n in have if n.endswith(".img") and n[:-4] not in want]

    fmt = {"IMG2": 0, "IMG1": 0}
    for n in have:
        if n.endswith(".img"):
            m = have[n][1].decode("ascii", "replace")
            fmt[m] = fmt.get(m, 0) + 1
    snd = [n for n in have if n.endswith(".snd")]

    print(f"卡带: {cart}  ({total} 字节)")
    print(f"素材: {len(have)} 个 (IMG2 4bpp: {fmt['IMG2']}, IMG1 16bpp: {fmt['IMG1']}, 音效: {len(snd)})")
    print(f"游戏会用到的图: {len(want)} 张 (角色 {len(want) - len(PROPS)} 帧 + 道具 {len(PROPS)})")
    if missing:
        print(f"!! 缺 {len(missing)} 张: " + ", ".join(missing[:12]) +
              (" ..." if len(missing) > 12 else ""))
        print("   -> 游戏里对应的角色/道具会直接不显示(blit 拿到 nullptr 就 return)")
    else:
        print("OK: 游戏会取的每一张图都在卡带里")
    if extra:
        print(f"?? 卡带里多出来的图(没代码会取): {len(extra)} 张, 白占体积")
        print("   " + ", ".join(sorted(extra)[:12]) + (" ..." if len(extra) > 12 else ""))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
