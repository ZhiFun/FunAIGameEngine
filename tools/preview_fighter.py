#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""临时: 只把某角色的几个关键动作放大渲染出来, 方便人工核对像素画质量。"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_fighter as G   # noqa: E402
import gen_skyraider_hd as S   # noqa: E402

who = sys.argv[1] if len(sys.argv) > 1 else "keyspr"
acts = sys.argv[2].split(",") if len(sys.argv) > 2 else ["idle", "walk", "punch", "kick"]
zoom = int(sys.argv[3]) if len(sys.argv) > 3 else 4
cols = int(sys.argv[4]) if len(sys.argv) > 4 else 6
out = sys.argv[5] if len(sys.argv) > 5 else "char_zoom.png"

items = []
for a in acts:
    A = G.ACTIONS[a]
    for i, k in enumerate(G.frames_of(A["keys"], A["frames"], A["cyclic"])):
        items.append((f"{who}_{a}{i}", G.make_char_frame(who, k, A["eyes"])))

G.preview(items, zoom=zoom, cols=cols, path=ROOT / "tools" / "_preview" / out)
print(f"{len(items)} 帧 -> tools/_preview/{out}")

# 单个英雄姿势的超大图(看细节)
single = G.make_char_frame(who, G.frames_of(G.ACTIONS["idle"]["keys"], 2, True)[0], "normal")
G.preview([("hero", single)], zoom=8, cols=1,
          path=ROOT / "tools" / "_preview" / f"{who}_hero8x.png")
print(f"hero -> tools/_preview/{who}_hero8x.png")
