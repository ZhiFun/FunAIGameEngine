#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_keychase.py —— KEY CHASE(键帽迷宫追逐)素材 + 迷宫生成器

玩法: 迷宫吃豆类(迷宫追逐/吃光所有豆过关)。美术与关卡**全部原创**:
  · 迷宫是本项目自己设计的 30x9 横向迷宫(屏就只有 428x142, 塞不下方迷宫),
    生成器会做连通性校验 —— 豆必须全部可达、鬼屋必须封闭(玩家进不去)。
  · 主角是"键帽小精灵": 一颗会张合的键帽, 主题跟键盘精灵一致。
  · 四个追兵是"键帽幽灵": 圆角键帽顶 + 波浪下摆, 各自带一件小配件区分
    (天线 / 面罩 / 犄角 / 顶盖), 颜色、名字都不是别人的。
  · 音效与循环紧张音(bgm_siren)也是自己合成的方波, 不是采样。

为什么迷宫放在生成器里: 它是**关卡数据**, 放在这里能自动化校验(见 validate_maze),
再把结果写成 games/keychase/maze_data.h 给 C++ 用。改迷宫只改下面的 MAZE。

输出:
  games/keychase/maze_data.h          迷宫(校验通过后生成)
  assets-src/keychase/*.png           精灵(锁 16 色 -> 打包时自动走 4bpp)
  assets-src/keychase/*.wav           音效 + 循环紧张音
  tools/_preview/keychase.png         预览总图

用法: py -3 tools\\gen_keychase.py
      py -3 tools\\gen_keychase.py --zoom 4      # 额外出放大预览
"""
import argparse
import math
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_skyraider_hd as S    # noqa: E402  复用画布/PNG/WAV/chiptune 原语

OUT = ROOT / "assets-src" / "keychase"
PREVIEW = ROOT / "tools" / "_preview" / "keychase.png"
HEADER = ROOT / "games" / "keychase" / "maze_data.h"

KEY = S.KEY
INK = (10, 13, 22)

COLS, ROWS = 30, 9          # 迷宫格数(格 14px -> 420x126, 正好放进 428x142)
TILE = 14

# ===========================================================================
# 1) 迷宫(唯一真源)
#    '#' 墙   '.' 豆   'o' 大力丸   '-' 鬼屋门(玩家不能过)   ' ' 空地   'P' 玩家起点
#    横向 4 条走廊(1/3/5/7 行), 2/6 行是带竖井的墙, 4 行是隧道 + 鬼屋。
#    隧道在 0/29 列, 左右环绕; 鬼屋在 4 行的 13..16 列, 门在 3 行 14 列。
# ===========================================================================
def wall_row(gaps):
    """带竖井的墙行: 默认全是墙, gaps 里的列打通成通路。"""
    s = ["#"] * COLS
    for c in gaps:
        s[c] = "."
    s[0] = s[COLS - 1] = "#"
    return "".join(s)


MAZE = [
    "#" * COLS,                                                       # 0 外墙
    "#o" + "." * 12 + "##" + "." * 12 + "o#",                           # 1 上走廊(四角大力丸)
    wall_row([4, 9, 13, 20, 25]),                                     # 2 竖井(含鬼屋门正上方)
    "#" + "." * 11 + "#" + "-" + "####" + "." * 11 + "#",             # 3 中上走廊 + 鬼屋顶/门
    "  " + "." * 10 + "#" + "    " + "#" + "." * 10 + "  ",           # 4 隧道行 + 鬼屋内部
    "#" + "." * 11 + "######" + "." * 11 + "#",                       # 5 中下走廊 + 鬼屋底
    wall_row([6, 11, 18, 24]),                                        # 6 竖井
    "#o" + "." * 12 + "P" + "." * 13 + "o#",                          # 7 下走廊(中间是玩家起点)
    "#" * COLS,                                                       # 8 外墙
]

GHOST_HOME = [(13, 4), (14, 4), (15, 4), (16, 4)]     # 鬼屋内 4 个站位 (col,row)
GHOST_DOOR = (13, 3)                                   # 门(正上方是竖井 13 列, 鬼出得来)
FRUIT_SPOT = (15, 7)                                   # 水果出现的格子
SCATTER = [(1, 1), (28, 1), (28, 7), (1, 7)]           # 4 个巡逻角

POWER = {"o"}


def tile_at(c, r):
    """取格子字符; 列方向环绕(隧道), 行越界算墙。"""
    if r < 0 or r >= ROWS:
        return "#"
    return MAZE[r][c % COLS]


def walkable(c, r, want_door=False):
    t = tile_at(c, r)
    if t == "#":
        return False
    if t == "-":
        return want_door
    return True


def find_char(ch):
    for r, line in enumerate(MAZE):
        i = line.find(ch)
        if i >= 0:
            return (i, r)
    return None


def validate_maze():
    """迷宫自检: 尺寸 / 唯一起点 / 豆全可达 / 鬼屋封闭。不通过就直接报错退出。"""
    problems = []
    for i, line in enumerate(MAZE):
        if len(line) != COLS:
            problems.append(f"第 {i} 行长度 {len(line)} != {COLS}")

    start = find_char("P")
    if start is None:
        problems.append("没有玩家起点 'P'")
    if sum(line.count("P") for line in MAZE) != 1:
        problems.append("玩家起点必须恰好一个")

    # 鬼屋必须是封闭的: 从玩家起点出发(不穿门)不能走到鬼屋内部
    if start:
        seen = set()
        q = deque([start])
        seen.add(start)
        while q:
            c, r = q.popleft()
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = ((c + dc) % COLS, r + dr)
                if n in seen or not walkable(n[0], n[1], want_door=False):
                    continue
                seen.add(n)
                q.append(n)

        dots = [(c, r) for r in range(ROWS) for c in range(COLS)
                if MAZE[r][c] in ".o"]
        unreachable = [d for d in dots if d not in seen]
        if unreachable:
            problems.append(f"{len(unreachable)} 颗豆走不到, 例如 {unreachable[:5]}")

        inside = [g for g in GHOST_HOME if g in seen]
        if inside:
            problems.append(f"鬼屋没封住, 玩家能走进 {inside}")
        if GHOST_DOOR not in [d for d in dots]:
            pass                                   # 门不在豆列表里是正常的

        # 每个门的上下方要是通路(否则鬼出不来)
        dc, dr = GHOST_DOOR
        if not walkable(dc, dr - 1, want_door=False):
            problems.append(f"门的正上方 ({dc},{dr-1}) 不是通路, 鬼出不来")
        if not walkable(dc, dr + 1, want_door=True):
            problems.append(f"门的下方不是鬼屋内部: ({dc},{dr+1})")

    if problems:
        print("!! 迷宫校验不通过:")
        for p in problems:
            print("   " + p)
        return None

    dots = sum(line.count(".") for line in MAZE)
    powers = sum(line.count("o") for line in MAZE)
    return dict(dots=dots, powers=powers, start=start, reachable=len(seen))


def maze_lines():
    """把 'P' 换回普通地面(起点那格没有豆, 与街机惯例一致)。"""
    return [line.replace("P", " ") for line in MAZE]


def write_header(info):
    lines = maze_lines()
    body = []
    body.append("// 本文件由 tools/gen_keychase.py 生成 —— 改迷宫请改生成器里的 MAZE, 别手改这里。")
    body.append("#pragma once")
    body.append("")
    body.append("namespace games { namespace keychase {")
    body.append("")
    body.append(f"constexpr int kMazeCols = {COLS};")
    body.append(f"constexpr int kMazeRows = {ROWS};")
    body.append("")
    body.append("// 瓦片: '#' 墙  '.' 豆  ' ' 空地  '-' 鬼屋门(只有鬼能过)")
    body.append("constexpr char kMaze[kMazeRows][kMazeCols + 1] = {")
    for line in lines:
        body.append('    "%s",' % line)
    body.append("};")
    body.append("")
    body.append(f"constexpr int kDotCount   = {info['dots']};")
    body.append(f"constexpr int kPowerCount = {info['powers']};")
    body.append(f"constexpr int kStartCol   = {info['start'][0]};")
    body.append(f"constexpr int kStartRow   = {info['start'][1]};")
    body.append("")
    body.append(f"constexpr int kHomeCount  = {len(GHOST_HOME)};")
    body.append("constexpr int kHomeCol[kHomeCount] = {%s};"
                % ", ".join(str(c) for c, _ in GHOST_HOME))
    body.append("constexpr int kHomeRow[kHomeCount] = {%s};"
                % ", ".join(str(r) for _, r in GHOST_HOME))
    body.append("")
    body.append(f"constexpr int kScatterCol[{len(SCATTER)}] = {{%s}};"
                % ", ".join(str(c) for c, _ in SCATTER))
    body.append(f"constexpr int kScatterRow[{len(SCATTER)}] = {{%s}};"
                % ", ".join(str(r) for _, r in SCATTER))
    body.append("")
    body.append(f"constexpr int kDoorCol   = {GHOST_DOOR[0]};")
    body.append(f"constexpr int kDoorRow   = {GHOST_DOOR[1]};")
    body.append(f"constexpr int kFruitCol  = {FRUIT_SPOT[0]};")
    body.append(f"constexpr int kFruitRow  = {FRUIT_SPOT[1]};")
    body.append("")
    body.append("}} // namespace games::keychase")
    body.append("")
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text("\n".join(body), encoding="utf-8")
    return HEADER


# ===========================================================================
# 2) 图元(和 fighter 同一套做法: 锁色板 + 1px 描边 + 右下压暗)
# ===========================================================================
def round_rect(im, x, y, w, h, rad, col):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            dx = max(x + rad - xx, xx - (x + w - 1 - rad), 0)
            dy = max(y + rad - yy, yy - (y + h - 1 - rad), 0)
            if dx * dx + dy * dy <= rad * rad:
                im.set(xx, yy, col)


def shade_lower_right(im, x, y, w, h, hi, lo, k):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if im.get(xx, yy) == hi and (xx - x) * 0.5 + (yy - y) > (w * 0.5 + h) * k:
                im.set(xx, yy, lo)


# ---- 主角: 键盘精灵(12x12) -------------------------------------------------
# 造型照搬键盘固件主界面主题三那只键盘精灵(也是 KEY FIGHTER 里的 keyspr):
# 圆角机身 + 屏幕脸(青色眼睛) + 一排键帽 + 天线。12x12 里只能画"迷你版",
# 但它和游戏里那个角色是同一个色板, 一眼认得出来。
# 吃的动作靠"朝向前方的楔形缺口"张合表现(键帽小精灵张着机身边缘去呑)。
KB_HI  = (58, 108, 142)      # 机身亮
KB_MD  = (30, 66, 96)        # 机身中
KB_DK  = (15, 34, 56)        # 机身暗
KB_RIM = (20, 44, 68)        # 外框
KB_PANEL = (4, 14, 24)       # 屏幕底色
KB_EYE   = (112, 238, 255)   # 眼睛(主题青)
KB_GLOW  = (150, 248, 255)   # 高光/天线头


def muncher(direction, frame):
    """direction: 0=右 1=左 2=上 3=下; frame: 0 嘴小 1 嘴大。"""
    im = S.Img(12, 12, KEY)
    # 机身: 外框 + 内壳(右下压暗)
    round_rect(im, 1, 1, 10, 10, 3, KB_RIM)
    round_rect(im, 2, 2, 8, 8, 2, KB_HI)
    shade_lower_right(im, 2, 2, 8, 8, KB_HI, KB_MD, 0.75)

    # 屏幕脸(按朝向把脸偏一点, 看起来像在看路)
    fx = {0: 1, 1: -1}.get(direction, 0)
    fy = {2: -1, 3: 1}.get(direction, 0)
    round_rect(im, 3 + fx, 3 + fy, 6, 4, 1, KB_PANEL)
    ex, ey = 4 + fx, 4 + fy
    if direction == 2:                       # 朝上: 眼睛压到屏幕上方
        im.rect(ex, ey, 2, 2, KB_EYE)
        im.rect(ex + 3, ey, 2, 2, KB_EYE)
        im.set(ex, ey, KB_GLOW)
        im.set(ex + 3, ey, KB_GLOW)
    else:
        im.rect(ex, ey + 1, 2, 2, KB_EYE)
        im.rect(ex + 3, ey + 1, 2, 2, KB_EYE)
        im.set(ex, ey + 1, KB_GLOW)
        im.set(ex + 3, ey + 1, KB_GLOW)

    # 一排键帽(键盘的身份特征)
    for i in range(3):
        im.rect(3 + i * 2, 8 + fy, 2, 2, KB_MD)
        im.set(3 + i * 2, 8 + fy, KB_HI)

    # 天线(还带着一点惯性摆动)
    sway = 1 if frame else 0
    im.rect(6, 0, 1, 2, KB_RIM)
    im.rect(6 + sway, -1, 1, 1, KB_RIM)
    im.set(6 + sway, 0, KB_GLOW)

    # 嘴: 朝哪边就挖哪边的楔形缺口(frame 决定张多大)
    deep = 3 if frame == 0 else 5
    if direction == 0:
        for i in range(deep):
            im.rect(11 - i, 5 + i, 1 + i, 1, KEY)
            im.rect(11 - i, 6 - i, 1 + i, 1, KEY)
    elif direction == 1:
        for i in range(deep):
            im.rect(i, 5 + i, 1 + i, 1, KEY)
            im.rect(i, 6 - i, 1 + i, 1, KEY)
    elif direction == 2:
        for i in range(deep):
            im.rect(4 + i, i, 1, 1 + i, KEY)
            im.rect(7 - i, i, 1, 1 + i, KEY)
    else:
        for i in range(deep):
            im.rect(4 + i, 11 - i, 1, 1 + i, KEY)
            im.rect(7 - i, 11 - i, 1, 1 + i, KEY)

    im.outline(INK)
    return im


# ---- 追兵: 键帽幽灵(12x12, 圆角键帽顶 + 波浪下摆) --------------------------
GHOSTS = [
    # (名字, 亮, 中, 暗, 配件)
    ("g1", (240, 96, 104), (168, 44, 60), (100, 22, 36), "antenna"),
    ("g2", (110, 224, 236), (40, 140, 168), (20, 78, 104), "visor"),
    ("g3", (128, 150, 246), (58, 70, 168), (28, 34, 100), "horns"),
    ("g4", (250, 186, 92), (186, 118, 32), (114, 66, 16), "cap"),
]
GHOST_EYE = (248, 250, 255)
GHOST_PUPIL = (34, 52, 108)
SCARED_HI = (236, 240, 252)
SCARED_MD = (150, 158, 190)
SCARED_DK = (96, 104, 140)
SCARED_EYE = (60, 74, 132)


def ghost_body(im, hi, md, dk, accessory, wobble, pupil_dir):
    round_rect(im, 1, 1, 10, 9, 3, hi)
    shade_lower_right(im, 1, 1, 10, 10, hi, md, 0.72)
    # 波浪下摆: 两帧交错, 跑起来会"飘"
    off = 0 if wobble == 0 else 1
    for x in range(1, 11):
        if ((x + off) // 2) % 2 == 0:
            im.rect(x, 10, 1, 1, md)
            im.rect(x, 9, 1, 1, md)
        else:
            im.rect(x, 10, 1, 1, dk)
    im.rect(1, 10, 10, 1, dk)
    im.set(1, 10, KEY)
    im.set(5, 10, KEY)
    im.set(9, 10, KEY)
    # 眼睛 + 瞳孔(朝移动方向偏)
    for ex in (3, 7):
        im.rect(ex, 4, 2, 3, GHOST_EYE)
    px = {0: 1, 1: -1}.get(pupil_dir, 0)
    py = {2: 1, 3: -1}.get(pupil_dir, 0)
    im.rect(3 + max(0, px), 4 + max(0, py), 2 if px == 0 else 1, 2, GHOST_PUPIL)
    im.rect(7 + max(0, px), 4 + max(0, py), 2 if px == 0 else 1, 2, GHOST_PUPIL)
    # 配件(区分四个追兵, 也避免长得像别人家的鬼)
    if accessory == "antenna":
        im.rect(5, 0, 1, 2, md)
        im.set(5, 0, GHOST_EYE)
    elif accessory == "visor":
        im.rect(2, 3, 8, 1, dk)
    elif accessory == "horns":
        im.set(2, 1, md)
        im.set(9, 1, md)
        im.rect(2, 0, 1, 1, dk)
        im.rect(9, 0, 1, 1, dk)
    elif accessory == "cap":
        im.rect(3, 0, 6, 2, dk)
        im.rect(4, 0, 4, 1, md)
    im.outline(INK)
    return im


def scared_ghost(frame):
    im = S.Img(12, 12, KEY)
    ghost_body(im, SCARED_HI, SCARED_MD, SCARED_DK, None, frame, -1)
    # 害怕的脸: 两只方眼 + 一条波浪嘴
    for ex in (3, 7):
        im.rect(ex, 4, 2, 2, SCARED_EYE)
    im.rect(3, 8, 2, 1, SCARED_EYE)
    im.rect(5, 7, 2, 1, SCARED_EYE)
    im.rect(7, 8, 2, 1, SCARED_EYE)
    im.outline(INK)
    return im


def ghost_eyes(direction):
    """被吃掉之后只剩眼睛(身体掏空), 自己往回跑。"""
    im = S.Img(12, 12, KEY)
    for ex in (3, 7):
        im.rect(ex, 4, 2, 3, GHOST_EYE)
    px = {0: 1, 1: -1}.get(direction, 0)
    py = {2: 1, 3: -1}.get(direction, 0)
    for ex in (3, 7):
        im.rect(ex + max(0, px), 4 + max(0, py), 2 if px == 0 else 1, 2, GHOST_PUPIL)
    im.outline(INK)
    return im


# ---- 道具: 豆和大力量都是键帽 ----------------------------------------
CAP_TOP = (255, 255, 255)
CAP_HI  = (236, 240, 248)
CAP_MD  = (148, 156, 176)
CAP_DK  = (66, 74, 92)


def dot():
    """小键帽(取代传统的圆点): 顶面亮 + 侧面暗 + 底座, 1px 描边。"""
    im = S.Img(6, 6, KEY)
    round_rect(im, 0, 0, 6, 4, 1, CAP_HI)
    im.hline(0, 4, 6, CAP_MD)
    im.hline(0, 5, 6, CAP_DK)
    im.set(1, 0, CAP_TOP)
    im.set(1, 1, CAP_TOP)
    im.outline(INK)
    return im


def pellet(frame):
    """大力丸: 一颗大键帽(带发光顶面), 两帧闪。"""
    hi = (252, 216, 96) if frame == 0 else (198, 148, 40)
    lo = (168, 118, 24) if frame == 0 else (118, 78, 16)
    top = (255, 248, 208) if frame == 0 else (236, 208, 120)
    im = S.Img(10, 10, KEY)
    round_rect(im, 0, 0, 10, 6, 2, hi)
    im.rect(0, 6, 10, 3, lo)
    im.hline(0, 9, 10, INK)
    im.rect(1, 1, 8, 2, top)
    im.rect(2, 4, 6, 1, top)
    im.outline(INK)
    return im


FRUITS = [
    ("fruit0", (240, 96, 104), (150, 30, 44)),      # 键帽(红)
    ("fruit1", (250, 200, 96), (168, 118, 20)),     # 键帽(金)
    ("fruit2", (120, 210, 240), (30, 110, 150)),    # 键帽(青)
]


def fruit(idx):
    name, hi, lo = FRUITS[idx]
    im = S.Img(12, 12, KEY)
    round_rect(im, 1, 3, 10, 7, 2, hi)
    shade_lower_right(im, 1, 3, 10, 7, hi, lo, 0.7)
    im.rect(3, 5, 6, 2, (255, 252, 236))            # 键帽上的字符条
    im.rect(2, 1, 8, 2, lo)                         # 键帽顶面
    im.outline(INK)
    return im


def gen_image():
    items = []

    def save(name, im):
        S.write_png(OUT / f"{name}.png", im.w, im.h, im.px)
        items.append((name, im))

    for d, tag in ((0, "r"), (1, "l"), (2, "u"), (3, "d")):
        for f in (0, 1):
            save(f"muncher_{tag}{f}", muncher(d, f))
    for name, hi, md, dk, acc in GHOSTS:
        for f in (0, 1):
            save(f"ghost_{name}_{f}", ghost_body(S.Img(12, 12, KEY), hi, md, dk, acc, f, 0))
    for f in (0, 1):
        save(f"ghost_scared{f}", scared_ghost(f))
    for d, tag in ((0, "r"), (1, "l"), (2, "u"), (3, "d")):
        save(f"eyes_{tag}", ghost_eyes(d))
    save("dot", dot())
    for f in (0, 1):
        save(f"pellet{f}", pellet(f))
    for i in range(3):
        save(FRUITS[i][0], fruit(i))
    return items


# ===========================================================================
# 3) 音效 + 循环紧张音
# ===========================================================================
def siren_loop(samples_len=26460, f0=360.0, f1=820.0, cycles=6, vol=0.20):
    """自己合成的紧张音: 方波做上下扫频, 整段首尾相接可以无缝循环。"""
    out = []
    phase = 0.0
    for i in range(samples_len):
        t = i / samples_len
        f = f0 + (f1 - f0) * (0.5 - 0.5 * math.cos(2 * math.pi * cycles * t))
        phase += 2 * math.pi * f / 22050.0
        sq = 1.0 if math.sin(phase) >= 0 else -1.0
        env = 0.55 + 0.45 * math.sin(math.pi * (2 * cycles * t % 1.0))
        out.append(int(sq * vol * env * 32767))
    return out


def gen_audio():
    done = []

    def w(name, samples):
        S.write_wav(OUT / f"{name}.wav", samples)
        done.append(name)

    # 吃豆: 两个音高交替(经典"哇卡"), 由代码决定放哪一个
    w("sfx_chomp0", S.sweep(0.055, 620, 300, vol=0.30, duty=0.5, curve=0.7))
    w("sfx_chomp1", S.sweep(0.055, 380, 660, vol=0.30, duty=0.5, curve=0.7))
    # 大力丸
    w("sfx_pellet", S.arpeggio([S.M('C5'), S.M('G5')], 0.05, vol=0.32, duty=0.25))
    # 吃到追兵
    w("sfx_eatghost", S.arpeggio([S.M('G4'), S.M('C5'), S.M('E5'), S.M('G5')],
                                 0.04, vol=0.34, duty=0.25))
    # 吃水果
    w("sfx_fruit", S.arpeggio([S.M('E5'), S.M('A5'), S.M('C6')], 0.045, vol=0.30))
    # 被追兵抓到: 一段下滑
    w("sfx_death", S.arpeggio([S.M('C5'), S.M('A4'), S.M('F4'), S.M('D4'),
                               S.M('A3'), S.M('F3')], 0.075, vol=0.36, duty=0.5))
    # 过关
    w("sfx_level", S.arpeggio([S.M('C5'), S.M('E5'), S.M('G5'), S.M('C6'),
                               S.M('G5'), S.M('C6')], 0.06, vol=0.34))
    # 开局 3 声
    w("sfx_ready", S.arpeggio([S.M('C5'), S.M('C5'), S.M('G5')], 0.09, vol=0.32))
    # 加命
    w("sfx_life", S.arpeggio([S.M('C6'), S.M('G5'), S.M('C6'), S.M('E6')],
                             0.05, vol=0.30))
    # 循环紧张音(平时 / 大力丸期间变调)
    w("bgm_siren", siren_loop(samples_len=22050))
    w("bgm_siren_hot", siren_loop(samples_len=15435, f0=620.0, f1=1180.0, cycles=8,
                                  vol=0.22))
    return done


# ===========================================================================
# 4) 预览
# ===========================================================================
def preview(items, zoom=1, cols=10, path=None):
    pad = 3 * zoom
    cell_w = max(i.w for _, i in items) * zoom + pad
    cell_h = max(i.h for _, i in items) * zoom + pad + 4
    rows = (len(items) + cols - 1) // cols
    W, H = cols * cell_w + pad, rows * cell_h + pad
    sheet = S.Img(W, H, (24, 30, 46))
    for idx, (_, im) in enumerate(items):
        ox = pad + (idx % cols) * cell_w
        oy = pad + (idx // cols) * cell_h
        for y in range(im.h * zoom):
            for x in range(im.w * zoom):
                c = im.get(x // zoom, y // zoom)
                if c and c != KEY:
                    sheet.set(ox + x, oy + y, c)
    S.write_png(path or PREVIEW, W, H, sheet.px)
    return rows


def main():
    ap = argparse.ArgumentParser(description="生成 KEY CHASE 的迷宫与素材")
    ap.add_argument("--no-image", action="store_true")
    ap.add_argument("--no-audio", action="store_true")
    ap.add_argument("--zoom", type=int, default=0)
    a = ap.parse_args()

    info = validate_maze()
    if info is None:
        return 1
    print(f"迷宫 {COLS}x{ROWS}: 豆 {info['dots']} 颗, 大力丸 {info['powers']} 颗, "
          f"起点 {info['start']}, 玩家可达格数 {info['reachable']}")
    print(f"  -> {write_header(info)}")

    if not a.no_image:
        items = gen_image()
        rows = preview(items)
        if a.zoom > 1:
            preview(items, zoom=a.zoom, cols=8,
                    path=ROOT / "tools" / "_preview" / "keychase_zoom.png")
        kb = sum(len(i.px) for _, i in items) / 1024.0
        est = sum(len(i.px) // 6 + 40 for _, i in items) / 1024.0
        print(f"精灵: {len(items)} 张, 原始像素 {kb:.1f} KB -> {OUT}")
        print(f"  预览 {PREVIEW}   (4bpp 打包后约 {est:.1f} KB)")
        bad = [(n, len({tuple(i.px[k:k + 3]) for k in range(0, len(i.px), 3)}))
               for n, i in items
               if len({tuple(i.px[k:k + 3]) for k in range(0, len(i.px), 3)}) > 16]
        print(f"  锁色板(<=16): {'OK' if not bad else '超标 ' + str(bad)}")
    if not a.no_audio:
        names = gen_audio()
        total = sum((OUT / f"{n}.wav").stat().st_size for n in names)
        print(f"音频: {len(names)} 个, {total / 1024.0:.1f} KB")
        for n in names:
            print(f"    {n}.wav  {(OUT / (n + '.wav')).stat().st_size / 1024.0:6.1f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
