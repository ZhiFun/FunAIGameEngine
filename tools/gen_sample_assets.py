#!/usr/bin/env python3
"""生成示例素材(SKY RAIDER 用)到 assets-src/skyraider/。纯标准库。"""
import math
import random
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets-src" / "skyraider"
MAGENTA = (255, 0, 255)


def write_png(path, w, h, pixels):
    """pixels: bytes, 每像素 3 字节 RGB, 行序。真彩 PNG(无 alpha)。"""
    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)

    raw = b""
    row = w * 3
    for y in range(h):
        raw += b"\x00" + pixels[y * row:(y + 1) * row]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    Path(path).write_bytes(data)


def canvas(w, h, fill=MAGENTA):
    return bytearray(bytes(fill) * (w * h))


def put(cv, w, x, y, color):
    if 0 <= x < w and 0 <= y < len(cv) // (w * 3):
        i = (y * w + x) * 3
        cv[i], cv[i + 1], cv[i + 2] = color


def make_player():
    w, h = 24, 24
    cv = canvas(w, h)
    for y in range(h):
        for x in range(w):
            # 箭头形战机(白色机身 + 青色驾驶舱 + 黄色尾焰)
            body = (x <= 20 - y and x <= 20 - (h - 1 - y)) if 0 else False
            if 3 <= x <= 20 and 5 <= y <= 18 and abs(y - 11) <= (20 - x) * 0.6:
                put(cv, w, x, y, (230, 240, 255))
            if 6 <= x <= 12 and 9 <= y <= 13:
                put(cv, w, x, y, (90, 200, 255))
            if x <= 3 and 10 <= y <= 12:
                put(cv, w, x, y, (255, 210, 80))
    write_png(ASSETS / "player.png", w, h, cv)


def make_enemy():
    w, h = 20, 20
    cv = canvas(w, h)
    for y in range(h):
        for x in range(w):
            dx, dy = x - 9.5, y - 9.5
            if dx * dx + dy * dy <= 8 * 8:
                put(cv, w, x, y, (235, 60, 60))
            if dx * dx + dy * dy <= 3.5 * 3.5:
                put(cv, w, x, y, (120, 10, 30))
    write_png(ASSETS / "enemy.png", w, h, cv)


def make_bullet():
    w, h = 6, 6
    cv = canvas(w, h)
    for y in range(1, 5):
        for x in range(1, 5):
            put(cv, w, x, y, (255, 240, 120))
    write_png(ASSETS / "bullet.png", w, h, cv)


def write_wav(path, samples, rate=11025):
    with wave.open(str(path), "wb") as wv:
        wv.setnchannels(1)
        wv.setsampwidth(2)
        wv.setframerate(rate)
        wv.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(s)))) for s in samples))


def make_sfx():
    rate = 11025
    # 开火: 下滑方波
    n = int(0.12 * rate)
    out = []
    ph = 0.0
    for i in range(n):
        f = 880.0 + (300.0 - 880.0) * i / n
        ph += f / rate
        s = (1.0 if (ph % 1.0) < 0.5 else -1.0) * 0.4 * (1.0 - i / n)
        out.append(s * 32767)
    write_wav(ASSETS / "sfx_shoot.wav", out, rate)

    # 爆炸: 衰减噪声
    random.seed(7)
    n = int(0.30 * rate)
    out = []
    y = 0.0
    for i in range(n):
        k = i / n
        y += (random.uniform(-1, 1) - y) * 0.4
        out.append(y * math.exp(-k / 0.25) * 0.9 * 32767)
    write_wav(ASSETS / "sfx_explode.wav", out, rate)


def note(semi):
    return 440.0 * (2.0 ** (semi / 12.0))


def make_bgm():
    rate = 11025
    dur = 4.0
    n = int(dur * rate)
    out = [0.0] * n
    # G 大调简单琶音循环: G-B-D-E-D-B(每 0.25s 一个音), 低音 G2 每拍
    seq = ["G4", "B4", "D5", "E5", "D5", "B4", "G4", "B4",
           "C5", "E5", "G5", "E5", "D5", "B4", "G4", "D5"]
    semis = {"G4": 7, "B4": 11, "D5": 14, "E5": 16, "C5": 12, "G5": 19}
    step = 0.25
    for i, nm in enumerate(seq):
        f = note(semis[nm])
        start = int(i * step * rate)
        ln = int(step * 1.1 * rate)
        ph = 0.0
        for j in range(ln):
            if start + j >= n:
                break
            ph += f / rate
            s = (1.0 if (ph % 1.0) < 0.5 else -1.0)
            out[start + j] += s * 0.16 * min(1.0, j / 200)
    # 低音
    bass = {"G2": -17, "C3": -12, "D3": -10, "E3": -8}
    for bar in range(4):
        for i, nm in enumerate(("G2", "G2", "C3", "D3")):
            f = note(bass[nm])
            start = int((bar + i * 0.25) * rate)
            ln = int(0.25 * rate)
            ph = 0.0
            for j in range(ln):
                if start + j >= n:
                    break
                ph += f / rate
                out[start + j] += ((ph % 1.0) < 0.5) * 0.18
    write_wav(ASSETS / "bgm.wav", out, rate)


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    make_player()
    make_enemy()
    make_bullet()
    make_sfx()
    make_bgm()
    print(f"sample assets -> {ASSETS}")


if __name__ == "__main__":
    main()
