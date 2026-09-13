#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_skyraider_hd.py —— SKY RAIDER 高清素材生成器(纯标准库, 无第三方依赖)

参照 FC/GBA 时代的做法:
  · 精灵是小尺寸、有限色板、硬轮廓、明暗两档的像素画(不描边/不抗锯齿)
  · 有明确的"受击闪白"和"多帧动画"(尾焰、爆炸)
  · 音效是方波/三角波/噪声合成的 chiptune, 不是采样

输出:
  assets-src/skyraider/*.png    精灵与 UI
  assets-src/skyraider/*.wav    音效与 BGM
  tools/_preview/skyraider_hd.png  预览图(人工核对用)

用法:  py -3 tools\\gen_skyraider_hd.py
       py -3 tools\\gen_skyraider_hd.py --no-audio    # 只生成图片
       py -3 tools\\gen_skyraider_hd.py --no-image    # 只生成音频
"""
import argparse
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets-src" / "skyraider"
PREVIEW = ROOT / "tools" / "_preview" / "skyraider_hd.png"

RATE = 22050

# ---------------------------------------------------------------------------
# 调色板: 每种材质两档(亮/暗) + 一条近黑轮廓, 这是 FC 像素画的常见做法
# ---------------------------------------------------------------------------
KEY = (255, 0, 255)          # 透明色(打包器保留该值)

HULL_HI = (233, 246, 244)    # 我方机身 亮
HULL_LO = (150, 178, 196)    # 我方机身 暗
CYAN_HI = (108, 236, 228)
CYAN_LO = (36, 138, 166)
BLUE_HI = (96, 138, 240)
BLUE_LO = (44, 68, 156)
STEEL_HI = (176, 196, 210)
STEEL_LO = (86, 106, 128)
RED_HI = (238, 96, 96)
RED_LO = (152, 40, 48)
PUR_HI = (178, 118, 232)
PUR_LO = (98, 50, 142)
GOLD_HI = (252, 208, 88)
GOLD_LO = (196, 132, 36)
ORANGE_HI = (248, 148, 56)
ORANGE_LO = (188, 84, 28)
MINT_HI = (140, 244, 186)
MINT_LO = (58, 160, 106)
INK = (14, 18, 28)           # 轮廓
DARK = (26, 36, 54)          # 深色填充


# ---------------------------------------------------------------------------
# PNG 写出(与其它生成器同一套, 只用到 zlib/struct)
# ---------------------------------------------------------------------------
def write_png(path, width, height, pixels):
    def chunk(kind, data):
        body = struct.pack(">I", len(data)) + kind + data
        return body + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(pixels[y * width * 3:(y + 1) * width * 3]) for y in range(height))
    head = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head)
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


class Img:
    """最小画布: 逐像素写 + 几个图元 + 自动描边/高光。"""

    def __init__(self, w, h, fill=KEY):
        self.w, self.h = w, h
        self.px = bytearray(bytes(fill) * w * h)

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(c)

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            return (self.px[i], self.px[i + 1], self.px[i + 2])
        return None

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def tri(self, ax, ay, bx, by, cx, cy, col):
        """实心三角形(扫描线)。"""
        xs = sorted(((ax, ay), (bx, by), (cx, cy)), key=lambda p: p[1])
        (x0, y0), (x1, y1), (x2, y2) = xs
        if y2 == y0:
            return
        for y in range(y0, y2 + 1):
            def xat(xa, ya, xb, yb):
                if yb == ya:
                    return xa
                return xa + (xb - xa) * (y - ya) / (yb - ya)
            if y < y1:
                xa, xb = xat(x0, y0, x1, y1), xat(x0, y0, x2, y2)
            else:
                xa, xb = xat(x1, y1, x2, y2), xat(x0, y0, x2, y2)
            lo, hi = int(round(min(xa, xb))), int(round(max(xa, xb)))
            for x in range(lo, hi + 1):
                self.set(x, y, col)

    def ellipse(self, cx, cy, rx, ry, col):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if rx == 0 or ry == 0:
                    continue
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, col)

    def disc(self, cx, cy, r, col):
        self.ellipse(cx, cy, r, r, col)

    def outline(self, col=INK):
        """给所有不透明像素外面加一圈轮廓(FC 精灵的灵魂)。"""
        src = self.get
        todo = []
        for y in range(self.h):
            for x in range(self.w):
                c = src(x, y)
                if c is None or c == KEY:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    n = src(x + dx, y + dy)
                    if n is not None and n == KEY:
                        todo.append((x + dx, y + dy))
        for x, y in todo:
            self.set(x, y, col)

    def shade(self):
        """把所有"上面还有同色像素"的位置压暗一档, 产生体积感。"""
        pairs = {HULL_HI: HULL_LO, CYAN_HI: CYAN_LO, BLUE_HI: BLUE_LO, STEEL_HI: STEEL_LO,
                 RED_HI: RED_LO, PUR_HI: PUR_LO, GOLD_HI: GOLD_LO, ORANGE_HI: ORANGE_LO,
                 MINT_HI: MINT_LO}
        src = [(self.get(x, y), self.get(x, y - 1), x, y)
               for y in range(self.h) for x in range(self.w)]
        for cur, above, x, y in src:
            if cur in pairs and above in pairs and above == cur:
                self.set(x, y, pairs[cur])


def save(name, img):
    write_png(OUT / f"{name}.png", img.w, img.h, img.px)
    return name, img


# ---------------------------------------------------------------------------
# 精灵: 我方战机(24x16, 机头朝右) —— 3 帧尾焰
# ---------------------------------------------------------------------------
def ship(frame):
    im = Img(24, 16)
    # 主翼(从机身前缘向后掠)
    im.tri(6, 1, 6, 14, 15, 7, STEEL_HI)
    # 机身: 尖机头在右
    im.tri(2, 4, 2, 11, 22, 7, HULL_HI)
    # 机背高光
    im.hline(4, 5, 15, HULL_HI)
    # 座舱
    im.disc(13, 7, 2, CYAN_HI)
    im.set(13, 6, HULL_HI)
    # 机腹阴影
    im.hline(5, 10, 13, HULL_LO)
    # 尾喷口
    im.rect(1, 6, 2, 4, GOLD_HI)
    # 尾焰(三帧长度不同)
    if frame == 0:
        im.tri(1, 6, 1, 9, -2, 7, ORANGE_HI)
    elif frame == 1:
        im.rect(0, 7, 1, 2, GOLD_HI)
    else:
        im.rect(0, 6, 1, 4, ORANGE_HI)
        im.rect(0, 7, 1, 2, GOLD_HI)
    im.shade()
    im.outline()
    return im


# ---------------------------------------------------------------------------
# 精灵: 敌方三种(T-直飞 / X-蛇形 / 炮台)
# ---------------------------------------------------------------------------
def enemy_drone():
    im = Img(20, 14)
    im.tri(18, 3, 18, 10, 1, 7, RED_HI)      # 机头朝左(从右侧进场)
    im.tri(14, 0, 14, 13, 6, 7, RED_LO)      # 机翼
    im.disc(9, 7, 2, DARK)                   # 驾驶舱(深色)
    im.disc(9, 7, 1, GOLD_HI)
    im.rect(18, 6, 2, 2, ORANGE_HI)          # 尾焰
    im.shade()
    im.outline()
    return im


def enemy_weaver():
    im = Img(20, 14)
    im.tri(17, 2, 6, 7, 17, 6, PUR_HI)       # 上翼
    im.tri(17, 12, 6, 7, 17, 8, PUR_LO)      # 下翼
    im.ellipse(11, 7, 4, 2, PUR_HI)          # 机身
    im.disc(8, 7, 1, GOLD_HI)
    im.rect(17, 6, 2, 2, ORANGE_HI)
    im.shade()
    im.outline()
    return im


def enemy_turret():
    im = Img(22, 18)
    im.rect(4, 4, 16, 10, STEEL_HI)          # 主体
    im.rect(6, 2, 12, 14, STEEL_HI)
    im.rect(8, 6, 6, 6, RED_HI)              # 红芯
    im.disc(11, 9, 2, GOLD_HI)
    im.rect(0, 7, 6, 4, STEEL_LO)            # 炮管朝左
    im.rect(0, 8, 4, 2, INK)
    im.rect(6, 16, 10, 2, STEEL_LO)          # 底座
    im.shade()
    im.outline()
    return im


# ---------------------------------------------------------------------------
# 精灵: Boss(程序化, 大尺寸手画不现实) —— 两档
# ---------------------------------------------------------------------------
def boss(w, h, hull_hi, hull_lo, core_hi, pods=4):
    im = Img(w, h)
    cy = h // 2
    # 主舰体: 正面朝向玩家(左), 截面为拉长六边形
    im.rect(w // 4, h // 5, w // 2, h - 2 * (h // 5), hull_hi)
    im.tri(w // 4, h // 5, w // 4, h - h // 5, 2, cy, hull_hi)
    im.tri(w - w // 8, h // 5, w - w // 8, h - h // 5, w - 1, cy, hull_lo)
    # 上下装甲
    im.rect(w // 3, 0, w // 3, h // 5, hull_lo)
    im.rect(w // 3, h - h // 5, w // 3, h // 5, hull_lo)
    # 炮口(左边缘伸出)
    for k in range(pods):
        y = int(h * (k + 1) / (pods + 1))
        im.rect(0, y - 2, w // 4, 4, STEEL_LO)
        im.rect(0, y - 1, w // 6, 2, INK)
    # 核心(弱点)
    im.disc(w // 3, cy, min(w, h) // 7, DARK)
    im.disc(w // 3, cy, max(2, min(w, h) // 9), core_hi)
    im.disc(w // 3, cy, max(1, min(w, h) // 16), HULL_HI)
    # 侧面散热格栅
    for x in range(w // 2, w - w // 6, 4):
        im.rect(x, h // 3, 2, h // 3, hull_lo)
    im.shade()
    im.outline()
    return im


# ---------------------------------------------------------------------------
# 精灵: 掉落物 / 子弹 / 爆炸
# ---------------------------------------------------------------------------
def pickup_power():
    im = Img(16, 16)
    im.rect(2, 2, 12, 12, RED_LO)
    im.rect(3, 3, 10, 10, RED_HI)
    im.disc(8, 8, 3, HULL_HI)
    im.set(8, 6, KEY)
    im.outline()
    return im


def pickup_missile():
    im = Img(16, 16)
    for d in range(7):
        im.rect(8 - d, 8 - d, 2 * d + 1, 1, ORANGE_HI)
        im.rect(8 - d, 8 + d, 2 * d + 1, 1, ORANGE_LO)
    im.disc(8, 8, 3, GOLD_HI)
    im.disc(8, 8, 1, HULL_HI)
    im.outline()
    return im


def pickup_life():
    im = Img(16, 16)
    im.rect(2, 6, 12, 4, MINT_LO)
    im.rect(6, 2, 4, 12, MINT_LO)
    im.rect(3, 7, 10, 2, MINT_HI)
    im.rect(7, 3, 2, 10, MINT_HI)
    im.outline()
    return im


def bullet_a():
    im = Img(10, 4)
    im.rect(0, 1, 8, 2, GOLD_HI)
    im.rect(6, 0, 4, 4, HULL_HI)
    im.set(9, 1, GOLD_LO)
    im.set(9, 2, GOLD_LO)
    im.outline()
    return im


def bullet_b():
    im = Img(12, 6)
    im.rect(0, 2, 8, 2, CYAN_HI)
    im.tri(6, 0, 6, 5, 11, 2, HULL_HI)
    im.outline()
    return im


def bullet_c():
    im = Img(14, 6)
    im.rect(0, 2, 8, 2, PUR_HI)
    im.tri(5, 0, 5, 5, 13, 2, MINT_HI)
    im.outline()
    return im


def boom(size, rings):
    im = Img(size, size)
    c = size // 2
    for k, (rad, col) in enumerate(rings):
        im.disc(c, c, rad, col)
    im.outline(DARK)
    return im


# ---------------------------------------------------------------------------
# 背景 / HUD
# ---------------------------------------------------------------------------
def nebula(w, h):
    im = Img(w, h, (6, 10, 24))
    # 垂直渐变
    for y in range(h):
        t = y / max(1, h - 1)
        col = (int(6 + 10 * t), int(10 + 26 * t), int(24 + 46 * t))
        im.hline(0, y, w, col)
    # 星云(几个巨大的柔光椭圆, 用两层叠出层次)
    for cx, cy, rx, ry, col in ((90, 40, 90, 34, (22, 30, 68)),
                                (300, 96, 110, 40, (30, 26, 64)),
                                (210, 20, 70, 26, (18, 40, 70))):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                dx = (x - cx) / max(1, rx)
                dy = (y - cy) / max(1, ry)
                d = dx * dx + dy * dy
                if d <= 1.0:
                    base = im.get(x, y)
                    if base is None:
                        continue
                    k = 1.0 - d
                    mixed = (min(255, int(base[0] + col[0] * k * 0.9)),
                             min(255, int(base[1] + col[1] * k * 0.9)),
                             min(255, int(base[2] + col[2] * k * 0.9)))
                    im.set(x, y, mixed)
    # 星星(固定种子, 可复现)
    seed = 0x2A3B4C
    for _ in range(260):
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        x = (seed >> 8) % w
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        y = (seed >> 8) % h
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        b = 90 + (seed >> 24) % 160
        im.set(x, y, (b, b, min(255, b + 24)))
    return im


def planet(size):
    im = Img(size, size)
    c = size // 2
    r = c - 2
    im.disc(c, c, r, (58, 84, 140))
    # 高光(左上)
    im.disc(c - r // 3, c - r // 3, r // 2, (96, 132, 190))
    im.disc(c - r // 3, c - r // 3, r // 4, (140, 178, 224))
    # 阴影(右下)
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            if dx * dx + dy * dy <= r * r and (dx + dy) > r * 0.7:
                col = im.get(x, y)
                if col and col != KEY:
                    im.set(x, y, (max(8, col[0] // 2), max(10, col[1] // 2), max(20, col[2] // 2)))
    # 光环
    for x in range(size):
        for y in range(size):
            dx = (x - c) / (r + 1)
            dy = (y - c) / (r * 0.28)
            d = abs(dx * dx + dy * dy - 1.0)
            if d < 0.10 and dx * dx + dy * dy > 0.2:
                im.set(x, y, (168, 148, 108))
    im.outline((10, 12, 22))
    return im


def hud_frame(w, h=16):
    im = Img(w, h)
    im.rect(0, 0, w, h, (10, 16, 30))
    im.hline(0, 0, w, (58, 110, 168))
    im.hline(0, h - 1, w, (26, 40, 66))
    im.hline(0, 2, w, (18, 28, 48))
    for x in range(6, w - 6, 96):            # 铆钉
        im.rect(x, 5, 2, 2, STEEL_LO)
        im.rect(x, h - 7, 2, 2, STEEL_LO)
    return im


# ---------------------------------------------------------------------------
# chiptune: 方波 / 三角波 / 噪声 + 音量包络
# ---------------------------------------------------------------------------
_rnd = [0x12345678]


def rnd():
    _rnd[0] = (_rnd[0] * 1664525 + 1013904223) & 0xFFFFFFFF
    return (_rnd[0] >> 8) / 8388608.0 - 1.0      # -1..1


def note_hz(midi):
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def env(i, n, attack=0.02, decay=0.25, sustain=0.55, release=0.35):
    """0..1 的包络; 位置按比例划分成 A/D/S/R 四段。"""
    t = i / max(1, n)
    if t < attack:
        return t / attack
    if t < attack + decay:
        k = (t - attack) / decay
        return 1.0 - (1.0 - sustain) * k
    if t < 1.0 - release:
        return sustain
    k = (t - (1.0 - release)) / release
    return sustain * (1.0 - k)


def render(dur, fn):
    """fn(i, n, t) -> 样本(-1..1); 统一转成 int16。"""
    n = int(RATE * dur)
    out = []
    for i in range(n):
        v = fn(i, n, i / max(1, n))
        out.append(int(max(-1.0, min(1.0, v)) * 31000))
    return out


def sweep(dur, f0, f1, vol=0.5, duty=0.5, curve=1.0, noise_mix=0.0,
          attack=0.01, decay=0.20, sustain=0.55, release=0.35):
    phase = [0.0]

    def f(i, n, t):
        hz = f0 + (f1 - f0) * (t ** curve)
        phase[0] += hz / RATE
        v = 1.0 if (phase[0] % 1.0) < duty else -1.0
        if noise_mix:
            v = v * (1.0 - noise_mix) + rnd() * noise_mix
        return v * env(i, n, attack, decay, sustain, release) * vol
    return render(dur, f)


def noise_burst(dur, vol=0.6, decay=0.6, lowpass=0.0):
    """爆炸: 噪声 + 低频轰鸣。"""
    phase = [0.0, 0.0]

    def f(i, n, t):
        nz = rnd()
        if lowpass:
            phase[1] += (nz - phase[1]) * lowpass
            nz = phase[1]
        phase[0] += (90.0 - 70.0 * t) / RATE          # 低频下扫
        rumble = math.sin(phase[0] * 2.0 * math.pi) * 0.45
        e = (1.0 - t) ** (1.0 + decay * 2.0)
        return (nz * 0.7 + rumble) * e * vol
    return render(dur, f)


def arpeggio(notes, step, vol=0.5, duty=0.5):
    """上/下行琶音(拾取、过关)。"""
    out = []
    for m in notes:
        out += sweep(step, note_hz(m), note_hz(m), vol=vol, duty=duty,
                     attack=0.02, decay=0.3, sustain=0.8, release=0.2)
    return out


def sequence(patterns, tempo=0.125, lead=0.30, bass=0.34, drums=True):
    """极简 chiptune 音序器: 主旋律(方波) + 贝斯(三角) + 噪声鼓。"""
    lead_track, bass_track, drum_track = patterns
    steps = len(lead_track)
    step_n = int(RATE * tempo)
    out = [0.0] * (steps * step_n)

    def put(idx, value):
        base = idx * step_n
        for i, v in enumerate(value):
            if base + i < len(out):
                out[base + i] += v

    for s, m in enumerate(lead_track):
        if m is None:
            continue
        put(s, sweep(tempo, note_hz(m), note_hz(m), vol=lead, duty=0.25,
                     attack=0.02, decay=0.15, sustain=0.8, release=0.30))
    for s, m in enumerate(bass_track):
        if m is None:
            continue
        seg = render(tempo, lambda i, n, t, hz=note_hz(m): (
            (4.0 * abs(((i / RATE) * hz) % 1.0 - 0.5) - 1.0) * 0.9
            * env(i, n, 0.02, 0.2, 0.85, 0.25)))
        put(s, [v * bass for v in seg])
    if drums:
        for s, d in enumerate(drum_track):
            if d == 0:
                continue
            if d == 1:                                  # 底鼓
                put(s, render(tempo * 0.5, lambda i, n, t: (
                    math.sin(i / RATE * 2 * math.pi * (140 - 90 * t)) * (1 - t) * 0.7)))
            else:                                       # 军鼓/踩镲
                put(s, render(tempo * 0.25, lambda i, n, t: rnd() * (1 - t) * 0.30))
    return [int(max(-32767, min(32767, v * 32000))) for v in out]


def write_wav(path, samples, rate=RATE):
    data = struct.pack("<%dh" % len(samples), *samples)
    head = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            + b"data" + struct.pack("<I", len(data)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + data)


# --- 音符(半音值, 60 = 中央 C) ---
N = None
def M(name):
    """音名 -> MIDI 值, 例如 M('C4') / M('A#3')。"""
    base = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}[name[0]]
    if len(name) > 2 and name[1] in '#b':
        base += 1 if name[1] == '#' else -1
    octave = int(name[-1])
    return 12 * (octave + 1) + base


def gen_audio():
    done = []

    def w(name, samples):
        write_wav(OUT / f"{name}.wav", samples)
        done.append(name)

    # 开火: 高频下扫方波, 干脆利落
    w("sfx_shoot", sweep(0.09, 1500, 420, vol=0.30, duty=0.25, noise_mix=0.12))
    # 命中: 更短更低
    w("sfx_hit", sweep(0.06, 900, 300, vol=0.26, duty=0.5, noise_mix=0.30))
    # 爆炸: 噪声
    w("sfx_explode", noise_burst(0.30, vol=0.55, decay=0.5, lowpass=0.25))
    # 大爆炸: 更长更闷
    w("sfx_explode_big", noise_burst(0.42, vol=0.70, decay=0.25, lowpass=0.10))
    # 自机受击: 下坠音
    w("sfx_player_hit", sweep(0.30, 700, 90, vol=0.55, duty=0.5, noise_mix=0.35,
                              decay=0.5, release=0.4))
    # 拾取: 上行琶音
    w("sfx_pick", arpeggio([M('C5'), M('E5'), M('G5'), M('C6')], 0.045, vol=0.34))
    # 加命: 更亮的琶音
    w("sfx_life", arpeggio([M('G4'), M('C5'), M('E5'), M('G5'), M('C6')], 0.055, vol=0.36))
    # 激光: 长扫 + 噪声
    w("sfx_laser", sweep(0.32, 260, 1800, vol=0.34, duty=0.5, curve=0.5, noise_mix=0.18,
                         attack=0.05, decay=0.6, sustain=0.9, release=0.25))
    # 导弹发射
    w("sfx_missile", sweep(0.30, 300, 1200, vol=0.30, duty=0.5, curve=0.6, noise_mix=0.4))
    # Boss 警告: 两声低沉
    w("sfx_boss_warn", arpeggio([M('C3'), M('F#3'), M('C3'), M('F#3')], 0.115, vol=0.42, duty=0.5))
    # 过关: 明快小号
    w("sfx_clear", arpeggio([M('C5'), M('E5'), M('G5'), M('C6'), M('G5'), M('C6'), M('E6')],
                            0.075, vol=0.38))
    # 游戏结束: 下行
    w("sfx_over", arpeggio([M('G4'), M('E4'), M('C4'), M('G3')], 0.12, vol=0.40, duty=0.5))
    # UI 确认
    w("sfx_ui", sweep(0.07, 1200, 1600, vol=0.26, duty=0.5))

    # ---- BGM: 关卡(循环) ----
    # A 小调进行 Am - F, 16 步/小节。卡带要放进设备的 SPIFFS(896KB)且单大文件
    # 在 SPIFFS 里可用比例很低, 所以循环短一点: 2 小节足够循环, 体量减半。
    lead = [M('A4'), N, M('C5'), N, M('E5'), N, M('C5'), N,
            M('F4'), N, M('A4'), N, M('C5'), N, M('A4'), N]
    bass = [M('A2'), N, M('A3'), N, M('A2'), N, M('A3'), N,
            M('F2'), N, M('F3'), N, M('F2'), N, M('F3'), N]
    drums = [1, 0, 2, 0, 2, 0, 2, 1, 1, 0, 2, 0, 2, 0, 2, 2]
    w("bgm_stage", sequence((lead, bass, drums), tempo=0.125))

    # ---- BGM: Boss(更快更紧张) ----
    lead2 = [M('D4'), M('D4'), N, M('A#3'), N, M('D4'), N, M('F4'),
             M('C4'), M('C4'), N, M('G#3'), N, M('C4'), N, M('D#4')]
    bass2 = [M('D2'), N, M('D3'), N, M('D2'), N, M('D3'), N,
             M('C2'), N, M('C3'), N, M('C2'), N, M('C3'), N]
    drums2 = [1, 2, 2, 2, 1, 2, 2, 2, 1, 2, 2, 2, 1, 2, 2, 2]
    w("bgm_boss", sequence((lead2, bass2, drums2), tempo=0.105))

    return done


def gen_image():
    items = []
    for i in range(3):
        items.append(save(f"ship_f{i}", ship(i)))
    items.append(save("ene_drone", enemy_drone()))
    items.append(save("ene_weaver", enemy_weaver()))
    items.append(save("ene_turret", enemy_turret()))
    items.append(save("boss_small", boss(48, 36, RED_HI, RED_LO, GOLD_HI, pods=3)))
    items.append(save("boss_large", boss(76, 56, STEEL_HI, STEEL_LO, RED_HI, pods=4)))
    items.append(save("pick_power", pickup_power()))
    items.append(save("pick_missile", pickup_missile()))
    items.append(save("pick_life", pickup_life()))
    items.append(save("bul_a", bullet_a()))
    items.append(save("bul_b", bullet_b()))
    items.append(save("bul_c", bullet_c()))
    items.append(save("boom_s", boom(18, [(8, ORANGE_HI), (5, GOLD_HI), (2, HULL_HI)])))
    items.append(save("boom_m", boom(30, [(14, ORANGE_LO), (11, ORANGE_HI), (7, GOLD_HI),
                                          (3, HULL_HI)])))
    items.append(save("boom_l", boom(48, [(23, RED_LO), (19, ORANGE_LO), (15, ORANGE_HI),
                                          (10, GOLD_HI), (4, HULL_HI)])))
    items.append(save("bg_nebula", nebula(428, 142)))
    items.append(save("bg_planet", planet(72)))
    items.append(save("hud_frame", hud_frame(428, 16)))
    return items


def preview(items):
    """把所有精灵排成一张预览图, 方便人工核对。"""
    pad = 6
    W = max(428, sum(i.w + pad for _, i in items[:8]))
    rows, x, y, h = [], pad, pad, 0
    placed = []
    for name, im in items:
        if x + im.w + pad > W:
            x = pad
            y += h + pad + 12
            h = 0
        placed.append((name, im, x, y))
        x += im.w + pad
        h = max(h, im.h)
    H = y + h + pad + 12
    sheet = Img(W, H, (18, 22, 34))
    for name, im, ox, oy in placed:
        for py in range(im.h):
            for px_ in range(im.w):
                c = im.get(px_, py)
                if c and c != KEY:
                    sheet.set(ox + px_, oy + py, c)
    write_png(PREVIEW, W, H, sheet.px)
    return len(placed)


def main():
    ap = argparse.ArgumentParser(description="生成 SKY RAIDER 的 FC/GBA 风格素材")
    ap.add_argument("--no-audio", action="store_true", help="跳过音频")
    ap.add_argument("--no-image", action="store_true", help="跳过图片")
    a = ap.parse_args()

    if not a.no_image:
        items = gen_image()
        n = preview(items)
        kb = sum(len(i.px) for _, i in items) / 1024.0
        print(f"图片: {len(items)} 张, 原始像素 {kb:.1f} KB -> {OUT}")
        print(f"  预览: {PREVIEW}")
        for name, im in items:
            print(f"    {name:<14} {im.w}x{im.h}")
    if not a.no_audio:
        names = gen_audio()
        total = sum((OUT / f"{n}.wav").stat().st_size for n in names)
        print(f"音频: {len(names)} 个, {total / 1024.0:.1f} KB -> {OUT}")
        for n in names:
            print(f"    {n}.wav")


if __name__ == "__main__":
    main()
