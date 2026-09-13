#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fighter.py —— KEY FIGHTER(格斗游戏)素材生成器 v2

v1 的问题(用户原话: "简单劣质"): 一个动作只有一张图 → 动作僵硬; 颜色随手写 →
没有调色板纪律; 手脚是光秃秃的粗线段 → 没有"卡通手脚"; 暗面是硬色阶 → 塑料感。

v2 照着 FC(以及一切正经 2D 游戏)的做法重写了美术层:
  1. **锁死调色板**: 每个角色固定 15 色(含 1px 近黑描边) + 1 个透明色 = 16 色。
     全图不超过 16 色 → pack_assets.py 可以编成 **4bpp 索引图**(等价 FC 的 CHR),
     每张图体积减半, 于是帧数翻几倍还有富余。validate() 会拦住越界颜色。
  2. **关键帧 + 中间帧**: 每个动作写 2~4 个关键骨架, 帧由骨架**线性插值**生成
     (传统动画的做法)。帧数 > 关键帧数时自动插出中间帧(出拳那张"收势")。
  3. **挤压拉伸 + 天线惯性**: 关键帧里带 sq(挤/拉) 与 ant(天线摆), 落地/受击压帧。
  4. **描边 + 网点过渡**: 1px 近黑描边(FC 精灵的灵魂); 明暗交界处用 2x2 网点
     插一档过渡, 比硬色阶柔和, 而且不增加颜色数。
  5. **卡通手脚**: 近侧/远侧分色(远侧整体压暗一档), 手是圆拳套(带手腕环),
     脚是圆头靴(带鞋尖朝向), 不是线段。

「键盘精灵」沿用键盘固件主界面主题三的形象(圆角机身 + 屏幕脸 + 键帽 + 天线),
并按用户要求把它当作整套角色的美术基准: 四个角色都带键帽元素
(铁拳的拳套是键帽、影忍腰上别键帽手里剑、重装的肩甲是键帽)。

输出:
  assets-src/fighter/<角色>_<动作><帧>.png    精灵(29 帧/角色)
  assets-src/fighter/*.wav                    10 个 chiptune 音效(无 BGM)
  tools/_preview/fighter_hd.png               全部姿势预览(人工核对用)

用法:  py -3 tools\\gen_fighter.py
       py -3 tools\\gen_fighter.py --zoom 2      # 额外输出放大预览
"""
import argparse
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_skyraider_hd as S          # noqa: E402  复用画布/PNG/WAV/chiptune 原语

OUT = ROOT / "assets-src" / "fighter"
PREVIEW = ROOT / "tools" / "_preview" / "fighter_hd.png"

KEY = S.KEY                            # 透明(打包器保留该值 → 4bpp 索引 0)
INK = (10, 13, 22)                     # 1px 描边

SW, SH = 42, 60                        # 精灵画布
GROUND = 58                            # 脚底(58 时描边正好落在 59 行内)

# ===========================================================================
# 1) 调色板: 每角色固定 15 色。键名对两类角色通用:
#      hi/md/dk     主体三档           sk_hi/sk_md  脸(键盘=屏幕面板)两档
#      hair         头发(键盘=键帽)    eye          眼睛/发光
#      gl_hi/gl_md  近/远侧拳套        bt_hi/bt_md  近/远侧靴子
#      acc/acc2     强调色两档
# ===========================================================================
SKINS = {
    # 键盘精灵 —— 配色取键盘固件主界面主题三
    "keyspr": dict(
        kind="kb", label="KEY SPRITE", blurb="键盘精灵",
        ink=INK,
        hi=(58, 108, 142), md=(30, 66, 96), dk=(15, 34, 56),
        sk_hi=(10, 26, 42), sk_md=(4, 14, 24),          # 屏幕面板(脸)
        hair=(38, 62, 88), eye=(112, 238, 255),         # 键帽 / 眼睛
        gl_hi=(96, 226, 250), gl_md=(28, 116, 156),     # 拳套(近/远)
        bt_hi=(48, 92, 126), bt_md=(20, 44, 68),
        acc=(150, 248, 255), acc2=(44, 156, 196),
    ),
    # 铁拳小子 —— 红背心 + 键帽拳套
    "boxer": dict(
        kind="human", label="IRON FIST", blurb="键帽拳套",
        ink=INK,
        hi=(236, 84, 78), md=(176, 44, 48), dk=(104, 22, 32),
        sk_hi=(246, 206, 166), sk_md=(186, 134, 96),
        hair=(38, 30, 38), eye=(34, 46, 80),
        gl_hi=(252, 214, 92), gl_md=(168, 122, 26),     # 键帽色拳套
        bt_hi=(238, 238, 244), bt_md=(132, 140, 158),
        acc=(252, 226, 130), acc2=(64, 96, 178),
    ),
    # 影忍 —— 深蓝忍装 + 红围巾 + 键帽手里剑
    "ninja": dict(
        kind="human", label="SHADOW", blurb="键帽手里剑",
        ink=INK,
        hi=(62, 80, 142), md=(32, 44, 92), dk=(16, 22, 48),
        sk_hi=(230, 192, 156), sk_md=(164, 120, 92),
        hair=(22, 24, 38), eye=(244, 246, 252),
        gl_hi=(46, 54, 92), gl_md=(20, 24, 46),
        bt_hi=(38, 44, 78), bt_md=(14, 18, 38),
        acc=(230, 64, 72), acc2=(112, 224, 238),
    ),
    # 重装 —— 绿装甲 + 键帽肩甲
    "brute": dict(
        kind="human", label="BULWARK", blurb="键帽肩甲",
        ink=INK,
        hi=(128, 174, 100), md=(74, 116, 64), dk=(38, 66, 40),
        sk_hi=(210, 176, 138), sk_md=(146, 112, 82),
        hair=(96, 84, 66), eye=(252, 228, 122),
        gl_hi=(204, 176, 80), gl_md=(118, 94, 30),
        bt_hi=(116, 124, 100), bt_md=(52, 58, 46),
        acc=(240, 88, 60), acc2=(160, 206, 128),
    ),
}
SKIN_ORDER = ["keyspr", "boxer", "ninja", "brute"]
MAX_COLORS = 16                        # 含透明色 —— 4bpp 的天花板


# ===========================================================================
# 2) 骨架关键帧
# ===========================================================================
def K(torso=(21, 30), head=(21, 12),
      armB=((12, 26), (9, 33), (11, 38)), armA=((30, 26), (33, 33), (31, 38)),
      legB=((18, 39), (16, 48), (16, 57)), legA=((24, 39), (26, 48), (26, 57)),
      sq=0.0, ant=0.0):
    return dict(torso=torso, head=head, armB=armB, armA=armA,
                legB=legB, legA=legA, sq=sq, ant=ant)


def dy(k, n, ant=None):
    """整体竖直平移 n(做呼吸/上下起伏), 可选改天线摆角。"""
    def pt(p):                       # 单点
        return (p[0], p[1] + n)

    def poly(p):                     # 折线(一串点)
        return tuple((x, y + n) for x, y in p)

    out = dict(k)
    out["torso"] = pt(k["torso"])
    out["head"] = pt(k["head"])
    for key in ("armB", "armA", "legB", "legA"):
        out[key] = poly(k[key])
    out["sq"] = k["sq"]
    out["ant"] = k["ant"] if ant is None else ant
    return out


def lerp(a, b, t):
    if isinstance(a, (int, float)):
        return a + (b - a) * t
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


def blend(k0, k1, t):
    return {key: lerp(k0[key], k1[key], t) for key in k0}


def frames_of(keys, n, cyclic):
    """把关键帧序列重采样成 n 帧。n == len(keys) 时就是关键帧本身。"""
    m = len(keys)
    if n <= 1 or m == 1:
        return [dict(keys[0])]
    out = []
    for i in range(n):
        pos = (i * m / n) if cyclic else (i * (m - 1) / (n - 1))
        seg = int(math.floor(pos))
        frac = pos - seg
        if cyclic:
            seg %= m
            nxt = (seg + 1) % m
        else:
            seg = min(seg, m - 2)
            nxt = seg + 1
        out.append(blend(keys[seg], keys[nxt], frac))
    return out


# ---- 基础姿势 ---------------------------------------------------------------
IDLE0 = K()
IDLE1 = dy(IDLE0, -1, ant=1.0)                          # 吸气: 整体上抬 1px

W0 = K(torso=(21, 30),                                  # 走: 近腿在前
       armB=((12, 26), (15, 33), (17, 38)), armA=((30, 26), (27, 33), (25, 38)),
       legB=((18, 39), (14, 48), (12, 57)), legA=((24, 39), (28, 48), (30, 57)),
       ant=-0.5)
W1 = K(torso=(21, 31),                                  # 走: 过渡(重心最低)
       armB=((12, 26), (14, 33), (15, 38)), armA=((30, 26), (29, 33), (28, 38)),
       legB=((18, 40), (16, 49), (15, 57)), legA=((24, 40), (26, 49), (27, 57)),
       ant=0.5)
W2 = K(torso=(21, 30),                                  # 走: 远腿在前
       armB=((12, 26), (16, 33), (18, 38)), armA=((30, 26), (28, 33), (26, 38)),
       legB=((18, 39), (22, 48), (24, 57)), legA=((24, 39), (20, 48), (18, 57)),
       ant=0.5)
W3 = dy(W1, -1, ant=-0.5)                               # 走: 过渡(重心最高)

CROUCH = K(torso=(21, 36), head=(21, 20),               # 蹲
           armB=((12, 32), (9, 38), (12, 43)), armA=((30, 32), (33, 38), (30, 43)),
           legB=((18, 45), (13, 51), (12, 57)), legA=((24, 45), (29, 51), (30, 57)),
           sq=0.35)

JUMP0 = K(torso=(21, 26), head=(21, 8),                 # 跳: 上升(收腿)
          armB=((12, 22), (8, 17), (10, 11)), armA=((30, 22), (34, 17), (32, 11)),
          legB=((18, 35), (13, 42), (15, 48)), legA=((24, 35), (29, 42), (27, 48)),
          sq=-0.3, ant=-1.0)
JUMP1 = K(torso=(21, 27), head=(21, 9),                 # 跳: 下落(伸腿)
          armB=((12, 23), (8, 26), (7, 31)), armA=((30, 23), (34, 26), (35, 31)),
          legB=((18, 36), (16, 46), (16, 55)), legA=((24, 36), (27, 46), (27, 55)),
          sq=0.2, ant=1.0)

P0 = K(torso=(20, 31), head=(20, 13),                   # 拳: 蓄力(拉回)
       armB=((12, 27), (10, 34), (13, 39)), armA=((29, 27), (26, 28), (22, 28)),
       legB=((17, 40), (14, 49), (14, 57)), legA=((23, 40), (26, 49), (27, 57)),
       sq=0.15, ant=0.6)
P1 = K(torso=(23, 30), head=(23, 12),                   # 拳: 命中(前手打满)
       armB=((14, 26), (12, 32), (16, 37)), armA=((30, 25), (34, 25), (38, 25)),
       legB=((19, 39), (15, 48), (13, 57)), legA=((25, 39), (29, 48), (31, 57)),
       sq=-0.2, ant=-0.8)
P2 = K(torso=(22, 30), head=(22, 12),                   # 拳: 收势
       armB=((13, 26), (11, 33), (14, 38)), armA=((30, 26), (33, 27), (36, 28)),
       legB=((18, 39), (15, 48), (14, 57)), legA=((24, 39), (27, 48), (29, 57)),
       ant=0.4)

KA0 = K(torso=(20, 31), head=(20, 13),                  # 踢: 抬膝
        armB=((12, 27), (9, 33), (11, 38)), armA=((29, 27), (32, 33), (30, 38)),
        legB=((17, 40), (15, 49), (15, 57)), legA=((23, 40), (27, 45), (24, 49)),
        sq=0.1, ant=0.5)
KA1 = K(torso=(19, 30), head=(19, 12),                  # 踢: 踹出(腿打直)
        armB=((11, 26), (7, 22), (5, 17)), armA=((28, 26), (31, 32), (29, 37)),
        legB=((17, 39), (14, 49), (14, 56)), legA=((23, 39), (30, 41), (37, 43)),
        sq=-0.25, ant=-1.0)
KA2 = K(torso=(20, 31), head=(20, 13),                  # 踢: 收腿
        armB=((12, 27), (10, 33), (12, 38)), armA=((29, 27), (32, 33), (30, 38)),
        legB=((17, 40), (15, 49), (15, 57)), legA=((23, 40), (28, 46), (29, 53)),
        ant=0.3)

CP0 = K(torso=(21, 36), head=(21, 20),                  # 蹲拳: 蓄力
        armB=((12, 32), (10, 38), (13, 43)), armA=((29, 32), (27, 33), (23, 33)),
        legB=((18, 45), (13, 51), (12, 57)), legA=((24, 45), (29, 51), (30, 57)),
        sq=0.35, ant=0.6)
CP1 = K(torso=(23, 36), head=(23, 20),                  # 蹲拳: 命中
        armB=((14, 32), (12, 38), (15, 43)), armA=((30, 31), (34, 31), (38, 31)),
        legB=((19, 45), (14, 51), (13, 57)), legA=((25, 45), (30, 51), (31, 57)),
        sq=0.3, ant=-0.7)

CK0 = K(torso=(21, 36), head=(21, 20),                  # 蹲踢: 抬脚
        armB=((12, 32), (9, 38), (12, 43)), armA=((29, 32), (32, 38), (30, 43)),
        legB=((18, 45), (15, 52), (16, 57)), legA=((24, 45), (29, 50), (26, 54)),
        sq=0.35, ant=0.4)
CK1 = K(torso=(20, 36), head=(20, 20),                  # 蹲踢: 踹出(贴地)
        armB=((11, 32), (8, 38), (11, 43)), armA=((28, 32), (31, 38), (29, 43)),
        legB=((18, 45), (14, 52), (14, 57)), legA=((24, 45), (31, 50), (37, 54)),
        sq=0.25, ant=-0.6)
CK2 = K(torso=(21, 36), head=(21, 20),                  # 蹲踢: 收
        armB=((12, 32), (9, 38), (12, 43)), armA=((29, 32), (32, 38), (30, 43)),
        legB=((18, 45), (15, 52), (16, 57)), legA=((24, 45), (28, 51), (27, 56)),
        sq=0.35, ant=0.2)

H0 = K(torso=(18, 31), head=(16, 13),                   # 受击: 后仰
       armB=((10, 27), (6, 23), (5, 17)), armA=((26, 27), (30, 22), (32, 15)),
       legB=((16, 40), (11, 49), (11, 57)), legA=((22, 40), (26, 50), (25, 57)),
       sq=0.3, ant=1.2)
H1 = K(torso=(19, 33), head=(17, 16),                   # 受击: 下沉回位
       armB=((11, 29), (8, 34), (10, 39)), armA=((27, 29), (31, 33), (30, 38)),
       legB=((17, 42), (13, 50), (13, 57)), legA=((23, 42), (27, 50), (27, 57)),
       sq=0.4, ant=0.6)

S0 = K(torso=(21, 30), head=(21, 12),                   # 超必杀: 举键(预备)
       armB=((12, 26), (9, 18), (13, 10)), armA=((30, 26), (33, 18), (29, 10)),
       legB=((18, 39), (15, 49), (15, 57)), legA=((24, 39), (28, 49), (29, 57)),
       sq=-0.35, ant=-1.2)
S1 = K(torso=(22, 32), head=(22, 14),                   # 超必杀: 下砸
       armB=((13, 28), (10, 34), (14, 40)), armA=((31, 28), (34, 34), (30, 40)),
       legB=((19, 41), (14, 50), (12, 57)), legA=((25, 41), (30, 50), (33, 57)),
       sq=0.5, ant=1.0)
S2 = K(torso=(22, 31), head=(22, 13),                   # 超必杀: 收势
       armB=((13, 27), (10, 33), (13, 39)), armA=((31, 27), (34, 33), (31, 39)),
       legB=((19, 40), (15, 49), (14, 57)), legA=((25, 40), (29, 49), (31, 57)),
       sq=0.2, ant=0.5)

KO0 = K(torso=(17, 31), head=(14, 15),                  # 倒地: 挨打后飞
        armB=((9, 27), (5, 22), (4, 16)), armA=((25, 27), (29, 21), (31, 14)),
        legB=((15, 40), (10, 48), (9, 55)), legA=((21, 40), (25, 48), (24, 55)),
        sq=0.35, ant=1.4)
KO1 = K(torso=(19, 43), head=(24, 47),                  # 倒地: 瘫在地上
        armB=((11, 41), (6, 46), (8, 52)), armA=((27, 41), (32, 46), (33, 52)),
        legB=((16, 47), (11, 52), (8, 55)), legA=((22, 47), (28, 52), (33, 55)),
        sq=0.5, ant=0.8)

# ---- 动作表: 关键帧 -> 帧数(帧数 > 关键帧数时自动插中间帧) ------------------
ACTIONS = {
    "idle":   dict(keys=[IDLE0, IDLE1],      frames=2, cyclic=True,  eyes="normal"),
    "walk":   dict(keys=[W0, W1, W2, W3],    frames=4, cyclic=True,  eyes="normal"),
    "crouch": dict(keys=[CROUCH],            frames=1, cyclic=False, eyes="normal"),
    "jump":   dict(keys=[JUMP0, JUMP1],      frames=2, cyclic=False, eyes="normal"),
    "punch":  dict(keys=[P0, P1, P2],        frames=4, cyclic=False, eyes="angry"),
    "kick":   dict(keys=[KA0, KA1, KA2],     frames=4, cyclic=False, eyes="angry"),
    "cpush":  dict(keys=[CP0, CP1],          frames=2, cyclic=False, eyes="angry"),
    "ckick":  dict(keys=[CK0, CK1, CK2],     frames=3, cyclic=False, eyes="angry"),
    "hurt":   dict(keys=[H0, H1],            frames=2, cyclic=False, eyes="x"),
    "super":  dict(keys=[S0, S1, S2],        frames=3, cyclic=False, eyes="angry"),
    "ko":     dict(keys=[KO0, KO1],          frames=2, cyclic=False, eyes="x"),
}
ACTION_ORDER = ["idle", "walk", "crouch", "jump", "punch", "kick",
                "cpush", "ckick", "hurt", "super", "ko"]


# ===========================================================================
# 3) 图元(只用调色板里的色, 保证不越界)
# ===========================================================================
def line(im, x0, y0, x1, y1, col):
    dx, dy_ = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy_
    while True:
        im.set(x0, y0, col)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy_:
            err += dy_
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


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
            if im.get(xx, yy) == hi and (xx - x) * 0.4 + (yy - y) > (w * 0.4 + h) * k:
                im.set(xx, yy, lo)


def dither_edge(im, x, y, w, h, lo, hi, phase=0):
    """只在"暗面紧贴亮面"的边界上打 2x2 网点: 硬色阶之间插一档过渡色, 不增色数。"""
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if im.get(xx, yy) != lo:
                continue
            near = any(im.get(xx + dx, yy + dyy) == hi
                       for dx, dyy in ((0, -1), (0, -2), (-1, 0), (-2, 0)))
            if near and (((xx >> 1) + (yy >> 1) + phase) & 1) == 0:
                im.set(xx, yy, hi)


def limb(im, pts, r, hi, lo, k=0.55):
    """粗管线: 主体亮色 + 右下侧压暗一档 -> 有体积的圆润手脚。"""
    shadow = []
    r2 = r * r
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        n = max(abs(x1 - x0), abs(y1 - y0)) + 1
        for s in range(n + 1):
            t = s / n
            cx = int(round(x0 + (x1 - x0) * t))
            cy = int(round(y0 + (y1 - y0) * t))
            for ddy in range(-r, r + 1):
                for ddx in range(-r, r + 1):
                    if ddx * ddx + ddy * ddy <= r2:
                        im.set(cx + ddx, cy + ddy, hi)
                        if ddx + ddy >= r * k:
                            shadow.append((cx + ddx, cy + ddy))
    for x, y in shadow:
        if im.get(x, y) == hi:
            im.set(x, y, lo)


def hand(im, hx, hy, hi, lo, keycap=False):
    """拳套: 圆手 + 右下压暗 + (键帽角色)顶面高光。"""
    im.disc(hx, hy, 3, hi)
    for yy in range(hy - 3, hy + 4):
        for xx in range(hx - 3, hx + 4):
            if im.get(xx, yy) == hi and (xx - hx) + (yy - hy) >= 2:
                im.set(xx, yy, lo)
    if keycap:
        im.rect(hx - 1, hy - 3, 3, 2, lo)
        im.set(hx, hy - 2, hi)


def boot(im, fx, fy, hi, lo, facing=1):
    """圆头靴: 脚背 + 朝前的鞋尖 + 鞋底压暗。"""
    x0 = fx - 3
    round_rect(im, x0, fy - 4, 8, 5, 2, hi)
    for yy in range(fy - 4, fy + 1):
        for xx in range(x0, x0 + 8):
            if im.get(xx, yy) == hi and (yy - (fy - 4)) >= 3:
                im.set(xx, yy, lo)
    im.set(fx + facing * 4, fy - 2, hi)
    im.set(fx + facing * 4, fy - 1, lo)


# ===========================================================================
# 4) 角色绘制
# ===========================================================================
def kb_face(im, x, y, w, h, sk, eyes, ant):
    """键盘精灵的机身 + 屏幕脸 + 键帽 + 天线。x,y = 左上角。"""
    round_rect(im, x, y, w, h, 4, sk["md"])
    round_rect(im, x + 1, y + 1, w - 2, h - 2, 3, sk["hi"])
    shade_lower_right(im, x + 1, y + 1, w - 2, h - 2, sk["hi"], sk["md"], 0.72)
    dither_edge(im, x + 1, y + 1, w - 2, h - 2, sk["md"], sk["hi"], phase=1)

    # 屏幕脸
    pw, ph = w - 6, max(5, (h - 2) // 2 - 1)
    px, py = x + 3, y + 2
    round_rect(im, px, py, pw, ph, 2, sk["sk_md"])
    im.hline(px + 1, py, pw - 2, sk["sk_hi"])
    ex, ey = px + pw // 2, py + ph // 2
    e, glow = sk["eye"], sk["acc"]
    if eyes == "normal":
        im.rect(ex - 6, ey - 1, 3, 3, e)
        im.rect(ex + 3, ey - 1, 3, 3, e)
        im.set(ex - 6, ey - 1, glow)
        im.set(ex + 3, ey - 1, glow)
    elif eyes == "angry":
        line(im, ex - 7, ey - 2, ex - 3, ey + 2, e)
        line(im, ex + 6, ey - 2, ex + 2, ey + 2, e)
    else:                                                    # x 眼
        for bx in (ex - 7, ex + 3):
            line(im, bx, ey - 2, bx + 3, ey + 2, e)
            line(im, bx + 3, ey - 2, bx, ey + 2, e)

    # 两排键帽
    ky = y + h - 7
    cols = (w - 4) // 4
    mid = cols // 2
    for row in range(2):
        for c in range(cols):
            kx = x + 3 + c * 4
            if kx + 3 > x + w - 3:
                break
            if row == 1 and c == mid:
                im.rect(kx, ky + row * 3, 3, 2, sk["acc2"])      # 一颗亮键
            else:
                im.rect(kx, ky + row * 3, 3, 2, sk["hair"])
            im.set(kx, ky + row * 3, sk["hi"])                   # 键帽顶面高光

    # 天线(带惯性摆动)
    ax = x + w // 2 + int(round(ant))
    im.rect(x + w // 2, y - 3, 1, 3, sk["acc2"])
    im.rect(ax, y - 5, 1, 2, sk["acc2"])
    im.disc(ax, y - 6, 1, glow)


def human_head(im, hx, hy, sk, name, eyes):
    im.disc(hx, hy, 6, sk["sk_hi"])
    for yy in range(hy - 6, hy + 7):
        for xx in range(hx - 6, hx + 7):
            if im.get(xx, yy) == sk["sk_hi"] and (yy - hy) >= 2:
                im.set(xx, yy, sk["sk_md"])
    if name == "boxer":                              # 竖起的短发
        im.rect(hx - 6, hy - 7, 12, 2, sk["hair"])
        im.rect(hx - 7, hy - 5, 2, 3, sk["hair"])
        im.rect(hx + 5, hy - 5, 2, 3, sk["hair"])
    elif name == "ninja":                            # 面罩 + 头巾 + 飘带
        im.rect(hx - 6, hy + 1, 13, 5, sk["md"])
        im.rect(hx - 7, hy - 7, 14, 5, sk["dk"])
        im.rect(hx + 5, hy - 4, 4, 2, sk["acc"])
        im.rect(hx + 9, hy - 6, 4, 1, sk["acc"])
    elif name == "brute":                            # 光头 + 络腮胡
        im.rect(hx - 5, hy + 3, 10, 3, sk["hair"])
        im.rect(hx - 2, hy - 7, 4, 2, sk["hair"])
    else:
        im.rect(hx - 5, hy - 7, 10, 2, sk["hair"])
    ex, ey = hx + 3, hy - 1
    e = sk["eye"]
    if eyes == "normal":
        im.rect(ex - 1, ey, 2, 2, e)
        im.rect(ex - 6, ey, 2, 2, e)
        im.set(ex, ey, sk["sk_hi"])
    elif eyes == "angry":
        line(im, ex - 2, ey - 1, ex + 1, ey + 1, e)
        line(im, ex - 6, ey + 1, ex - 3, ey - 1, e)
    else:
        for bx in (ex - 2, ex - 6):
            line(im, bx, ey - 1, bx + 3, ey + 2, e)
            line(im, bx + 3, ey - 1, bx, ey + 2, e)


def human_torso(im, sk, name, tx, ty, sq):
    w, h = 15, 18 - int(round(sq * 2))
    x, y = tx - w // 2, ty - h // 2
    round_rect(im, x, y, w, h, 4, sk["hi"])
    shade_lower_right(im, x, y, w, h, sk["hi"], sk["md"], 0.78)
    dither_edge(im, x, y, w, h, sk["md"], sk["hi"], phase=0)
    im.rect(x, ty + 6, w, 3, sk["dk"])                       # 腰带
    im.rect(tx - 2, ty + 6, 4, 3, sk["acc"])
    if name == "brute":                                      # 键帽肩甲
        for sx in (x - 1, x + w - 4):
            im.rect(sx, y - 1, 5, 3, sk["hair"])
            im.set(sx + 1, y - 1, sk["hi"])
            im.set(sx + 3, y - 1, sk["hi"])
    im.rect(tx - 2, y - 5, 4, 5, sk["sk_md"])                # 脖子


def draw_char(im, name, sk, k, eyes):
    kb = sk["kind"] == "kb"
    tx, ty = int(round(k["torso"][0])), int(round(k["torso"][1]))
    sq = k["sq"]

    armB = [(int(round(x)), int(round(y))) for x, y in k["armB"]]
    armA = [(int(round(x)), int(round(y))) for x, y in k["armA"]]
    legB = [(int(round(x)), int(round(y))) for x, y in k["legB"]]
    legA = [(int(round(x)), int(round(y))) for x, y in k["legA"]]
    if kb:                                    # 机身比人形躯干宽, 肩点挪到机身两侧
        armB[0] = (tx - 12, armB[0][1])
        armA[0] = (tx + 12, armA[0][1])

    # ---- 远侧(整体压暗一档, 制造前后关系)
    limb(im, legB, 3, sk["bt_md"], sk["bt_md"])
    boot(im, legB[-1][0], legB[-1][1], sk["bt_md"], sk["bt_md"], facing=-1)
    limb(im, armB, 3, sk["md"], sk["dk"])
    hand(im, armB[-1][0], armB[-1][1], sk["gl_md"], sk["gl_md"])

    # ---- 躯干 + 头
    if kb:
        kb_face(im, tx - 12, ty - 10, 25, 20, sk, eyes, k["ant"])
    else:
        human_torso(im, sk, name, tx, ty, sq)
        human_head(im, int(round(k["head"][0])), int(round(k["head"][1])), sk, name, eyes)

    # ---- 近侧
    limb(im, legA, 3, sk["bt_hi"], sk["bt_md"])
    boot(im, legA[-1][0], legA[-1][1], sk["bt_hi"], sk["bt_md"], facing=1)
    limb(im, armA, 3, sk["gl_hi"] if kb else sk["hi"], sk["md"])
    hand(im, armA[-1][0], armA[-1][1], sk["gl_hi"], sk["gl_md"],
         keycap=(name == "boxer"))
    if name == "ninja":                       # 腰上别一枚键帽手里剑
        im.rect(tx + 5, ty + 4, 3, 3, sk["sk_hi"])
        im.set(tx + 6, ty + 5, sk["acc2"])

    im.outline(INK)                           # 1px 近黑描边(FC 精灵的灵魂)
    return im


def make_char_frame(name, k, eyes):
    return draw_char(S.Img(SW, SH), name, SKINS[name], k, eyes)


# ===========================================================================
# 5) 招式道具(同样锁在有限色内)
# ===========================================================================
def keycap(w, h, cap=(236, 240, 248), cap_lo=(148, 156, 176),
           base=(66, 74, 92), base_lo=(34, 40, 54)):
    im = S.Img(w, h)
    top = max(2, h - max(2, h // 3))
    round_rect(im, 0, 0, w, top, max(1, w // 5), cap)
    shade_lower_right(im, 0, 0, w, top, cap, cap_lo, 0.7)
    im.rect(0, top, w, h - top, base)
    im.hline(0, h - 1, w, base_lo)
    im.outline(INK)
    return im


def cap_small():
    return keycap(9, 8)


def cap_big():
    return keycap(14, 12, cap=(252, 216, 96), cap_lo=(176, 130, 28),
                  base=(116, 78, 20), base_lo=(68, 44, 12))


def cap_tiny():
    return keycap(7, 6, cap=(184, 238, 255), cap_lo=(94, 158, 198),
                  base=(44, 78, 104), base_lo=(22, 42, 60))


def big_keyboard():
    """大招武器: 巨型键盘(双手抡起来砸人)。"""
    im = S.Img(92, 36, KEY)
    kb_face(im, 2, 2, 88, 30, SKINS["keyspr"], "angry", 0.0)
    im.rect(28, 32, 5, 4, SKINS["keyspr"]["md"])            # 握把
    im.rect(59, 32, 5, 4, SKINS["keyspr"]["md"])
    im.outline(INK)
    return im


def cap_minigun():
    """按键帽机关枪: 方管身 + 键帽弹鼓 + 喇叭口。"""
    im = S.Img(46, 22, KEY)
    sk = SKINS["keyspr"]
    body, body_lo = (86, 100, 126), (40, 48, 66)
    round_rect(im, 8, 7, 30, 9, 3, body)
    shade_lower_right(im, 8, 7, 30, 9, body, body_lo, 0.75)
    for c in range(3):                                      # 枪管上的键帽环
        cx = 30 + c * 5
        round_rect(im, cx, 6, 4, 4, 1, (236, 240, 248))
        round_rect(im, cx, 12, 4, 4, 1, (148, 156, 176))
    im.rect(0, 8, 8, 7, (52, 60, 78))                       # 枪托
    im.rect(40, 9, 6, 5, sk["hair"])                        # 喇叭口
    round_rect(im, 12, 0, 12, 8, 2, (196, 170, 78))         # 弹鼓
    for c in range(3):
        im.rect(14 + c * 4, 2, 2, 2, (250, 240, 200))
    im.outline(INK)
    return im


def spark():
    """命中火花: 四角星(两层, 中间白心)。"""
    im = S.Img(18, 18, KEY)
    im.tri(9, 0, 7, 8, 11, 8, (255, 248, 208))
    im.tri(9, 17, 7, 9, 11, 9, (255, 248, 208))
    im.tri(0, 9, 8, 7, 8, 11, (255, 232, 150))
    im.tri(17, 9, 9, 7, 9, 11, (255, 232, 150))
    im.disc(9, 9, 3, (255, 255, 244))
    im.outline((190, 104, 26))
    return im


def ring():
    """格挡/冲击波纹。"""
    im = S.Img(22, 22, KEY)
    for y in range(22):
        for x in range(22):
            d = (x - 11) ** 2 + (y - 11) ** 2
            if 64 <= d <= 100:
                im.set(x, y, (150, 216, 255) if d < 81 else (70, 140, 200))
    return im


def dizzy_star():
    im = S.Img(11, 11, KEY)
    for seg in ((5, 0, 5, 3), (5, 7, 5, 10), (0, 5, 3, 5), (7, 5, 10, 5)):
        line(im, seg[0], seg[1], seg[2], seg[3], (255, 236, 120))
    im.disc(5, 5, 3, (255, 244, 170))
    im.disc(5, 5, 1, (255, 255, 230))
    return im


def shadow():
    """脚下的落影: 三圈由外向内加深, 边缘不用网点(免得出现孤立噪点)。"""
    im = S.Img(28, 8, KEY)
    im.ellipse(14, 4, 13, 3, (44, 56, 84))
    im.ellipse(14, 4, 10, 3, (30, 40, 64))
    im.ellipse(14, 4, 6, 2, (20, 28, 48))
    return im


# ===========================================================================
# 6) 输出与校验
# ===========================================================================
def save(name, im):
    S.write_png(OUT / f"{name}.png", im.w, im.h, im.px)
    return name, im


def colors_of(im):
    seen = set()
    for i in range(0, len(im.px), 3):
        seen.add((im.px[i], im.px[i + 1], im.px[i + 2]))
    return seen


def validate(items):
    """锁色板纪律: 每张图 <= 16 色(含透明), 否则 4bpp 打包会退化成 16bpp。"""
    bad = []
    for name, im in items:
        c = colors_of(im)
        if len(c) > MAX_COLORS:
            bad.append((name, len(c)))
    if bad:
        print("!! 有素材超出 16 色, 4bpp 打包会失败:" + str(bad))
        return False
    return True


def gen_image():
    items = []
    for who in SKIN_ORDER:
        for act in ACTION_ORDER:
            a = ACTIONS[act]
            for i, k in enumerate(frames_of(a["keys"], a["frames"], a["cyclic"])):
                items.append(save(f"{who}_{act}{i}", make_char_frame(who, k, a["eyes"])))
    items.append(save("cap_small", cap_small()))
    items.append(save("cap_big", cap_big()))
    items.append(save("cap_tiny", cap_tiny()))
    items.append(save("big_kb", big_keyboard()))
    items.append(save("gun", cap_minigun()))
    items.append(save("spark", spark()))
    items.append(save("ring", ring()))
    items.append(save("dizzy", dizzy_star()))
    items.append(save("shadow", shadow()))
    return items


def preview(items, zoom=1, path=None, cols=8):
    pad = 4 * zoom
    cell_w = max(i.w for _, i in items) * zoom + pad
    cell_h = max(i.h for _, i in items) * zoom + pad + 4
    rows = (len(items) + cols - 1) // cols
    W, H = cols * cell_w + pad, rows * cell_h + pad
    sheet = S.Img(W, H, (24, 30, 46))
    for idx, (name, im) in enumerate(items):
        ox = pad + (idx % cols) * cell_w
        oy = pad + (idx // cols) * cell_h
        for y in range(im.h * zoom):
            for x in range(im.w * zoom):
                c = im.get(x // zoom, y // zoom)
                if c and c != KEY:
                    sheet.set(ox + x, oy + y, c)
    S.write_png(path or PREVIEW, W, H, sheet.px)
    return rows


# ===========================================================================
# 7) 音效(v1 不变)
# ===========================================================================
def gen_audio():
    done = []

    def w(name, samples):
        S.write_wav(OUT / f"{name}.wav", samples)
        done.append(name)

    w("sfx_swing", S.sweep(0.075, 2200, 700, vol=0.30, duty=0.5, noise_mix=0.85,
                           release=0.5))
    w("sfx_swing_heavy", S.sweep(0.12, 1200, 300, vol=0.34, duty=0.5, noise_mix=0.9,
                                 release=0.5))
    w("sfx_hit", S.noise_burst(0.11, vol=0.72, decay=0.5, lowpass=0.22))
    w("sfx_block", S.sweep(0.07, 1800, 2600, vol=0.30, duty=0.25, noise_mix=0.25))
    w("sfx_throw", S.sweep(0.10, 900, 300, vol=0.34, duty=0.125, curve=0.6,
                           noise_mix=0.35))
    w("sfx_clack", S.sweep(0.06, 2600, 1400, vol=0.34, duty=0.25, noise_mix=0.2))
    w("sfx_slam", S.noise_burst(0.30, vol=0.85, decay=0.35, lowpass=0.10))
    w("sfx_gun", S.sequence(([S.M('C6'), S.N, S.M('C6'), S.N,
                              S.M('C6'), S.N, S.M('C6'), S.N],
                             [None] * 8, [0] * 8), tempo=0.035,
                            lead=0.42, drums=False))
    w("sfx_ko", S.arpeggio([S.M('A3'), S.M('F3'), S.M('D3'), S.M('A2')], 0.085,
                           vol=0.46, duty=0.5))
    w("sfx_round", S.arpeggio([S.M('C5'), S.M('G5'), S.M('C6')], 0.075, vol=0.34))
    return done


def main():
    ap = argparse.ArgumentParser(description="生成 KEY FIGHTER 的 FC 风格素材")
    ap.add_argument("--no-audio", action="store_true")
    ap.add_argument("--no-image", action="store_true")
    ap.add_argument("--zoom", type=int, default=0,
                    help="额外输出放大预览(像素画核对用), 例如 --zoom 3")
    a = ap.parse_args()

    if not a.no_image:
        items = gen_image()
        rows = preview(items)
        if a.zoom > 1:
            props = {"cap_small", "cap_big", "cap_tiny", "big_kb", "gun",
                     "spark", "ring", "dizzy", "shadow"}
            chars = [(n, i) for n, i in items if n not in props]
            pr = [(n, i) for n, i in items if n in props]
            preview(chars, zoom=a.zoom, cols=len(ACTION_ORDER),
                    path=ROOT / "tools" / "_preview" / "fighter_chars_zoom.png")
            preview(pr, zoom=a.zoom, cols=5,
                    path=ROOT / "tools" / "_preview" / "fighter_props_zoom.png")
        kb = sum(len(i.px) for _, i in items) / 1024.0
        per_char = {w: sum(1 for n, _ in items if n.startswith(w + "_")) for w in SKIN_ORDER}
        print(f"图片: {len(items)} 张, 原始像素 {kb:.1f} KB ({rows} 行网格) -> {OUT}")
        print(f"  预览: {PREVIEW}")
        print(f"  锁色板校验: {'OK' if validate(items) else '不合格'}")
        print(f"  每角色帧数: {per_char} 共 {sum(per_char.values())} 帧")
        est = sum(len(i.px) // 2 + 40 for _, i in items) / 1024.0
        print(f"  4bpp 打包后素材体积约 {est:.1f} KB(不含音效)")
    if not a.no_audio:
        names = gen_audio()
        total = sum((OUT / f"{n}.wav").stat().st_size for n in names)
        print(f"音频: {len(names)} 个, {total / 1024.0:.1f} KB -> {OUT}")


if __name__ == "__main__":
    main()
