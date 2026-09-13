#!/usr/bin/env python3
"""把 assets-src/ 里的 PNG/WAV 打包成 assets-packed/ 里的 .img/.snd(纯标准库, 无依赖)。

  .img = "IMG1" + <HH w> <HH h> + w*h*2 字节 RGB565(小端)
  .snd = "SND1" + <II rate> <II frames> + frames*2 字节 int16 单声道

PNG 用 zlib 手写解码(支持灰度/真彩/调色板 + alpha); 透明像素用品红 #FF00FF 占位,
运行时可用 display.draw_image_alpha(..., kColorMagenta) 做 chroma-key。

音频存盘采样率是 SND_RATE(11025)而不是设备输出率 —— 卡带要进键盘的 SPIFFS 分区,
而引擎 AudioMixer 会按 clip->rate 换算步长, 所以时长/音高不变。详见 tools/sndlib.py。
"""
import os
import struct
import sys
import wave
import zlib
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "assets-src"
DST = Path(__file__).resolve().parent.parent / "assets-packed"
RATE = 22050          # 引擎/设备的输出采样率(不要改, AudioMixer 的输出固定是这个量级)
SND_RATE = 11025      # 素材存盘采样率: 减半存, 引擎按 clip->rate 换算回来, 时长音高不变


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"not a png: {path}"
    pos, idat = 8, b""
    w = h = bd = ct = 0
    plte = trns = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"PLTE":
            plte = chunk
        elif typ == b"tRNS":
            trns = chunk
        elif typ == b"IEND":
            break
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    bpp = ch * (bd // 8)
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out += line
        prev = line
    return w, h, ct, plte, trns, bytes(out)


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# 4bpp 索引图的适用上限(像素数)。索引图体积减半, 但运行时要逐像素查调色板;
# 大块不透明图(整屏背景)走 IMG1 的 memcpy 路径反而更快, 所以只给精灵用。
INDEXED_MAX_PIXELS = 4096


def pack_indexed(w, h, pix, colors):
    """<=16 色 -> 4bpp 索引图(等价 FC 的 CHR):

        "IMG2" | u16 w | u16 h | 16 * u16 调色板(RGB565) | ceil(w*h/2) 字节
        每个字节两个像素, 高半字节 = 左边那个; 行与行之间**不**补齐(线性编号)。
        索引 0 固定是透明(品红), 运行时不比色键, 直接跳过。

    体积是 RGB565 的一半, 而且调色板一锁, 颜色纪律自然就出来了。
    """
    mag = (255, 0, 255)
    pal = [mag] + [c for c in colors if c != mag][:15]
    idx = {c: i for i, c in enumerate(pal)}
    packed = bytearray((w * h + 1) // 2)
    for i, (_, c) in enumerate(pix):
        v = idx.get(c, 0) & 0x0F
        if i & 1:
            packed[i >> 1] |= v
        else:
            packed[i >> 1] |= (v << 4)
    head = b"IMG2" + struct.pack("<HH", w, h)
    head += b"".join(struct.pack("<H", rgb565(*c)) for c in pal)
    head += b"\x00" * (2 * (16 - len(pal)))
    return head + bytes(packed)


def pack_png(src, dst):
    w, h, ct, plte, trns, px = read_png(src)
    pal = []
    if ct == 3:
        for i in range(0, len(plte), 3):
            a = trns[i // 3] if i // 3 < len(trns) else 255
            pal.append((plte[i], plte[i + 1], plte[i + 2], a))
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]

    pix = []                                   # [(RGB565, (r,g,b))]
    for i in range(0, len(px), bpp):
        if ct == 0:
            r = g = b = px[i]; a = 255
        elif ct == 2:
            r, g, b, a = px[i], px[i + 1], px[i + 2], 255
        elif ct == 3:
            r, g, b, a = pal[px[i]]
        elif ct == 4:
            r = g = b = px[i]; a = px[i + 1]
        else:
            r, g, b, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        if a < 128:
            r, g, b = 255, 0, 255   # 透明 -> 品红(供 chroma-key / 索引 0)
        pix.append((rgb565(r, g, b), (r, g, b)))

    colors = sorted({c for _, c in pix})
    if len(colors) <= 16 and w * h <= INDEXED_MAX_PIXELS:
        dst.write_bytes(pack_indexed(w, h, pix, colors))
        return
    out = b"".join(struct.pack("<H", c565) for c565, _ in pix)
    dst.write_bytes(b"IMG1" + struct.pack("<HH", w, h) + out)


def pack_wav(src, dst):
    wv = wave.open(str(src), "rb")
    ch, sw, fr, n = wv.getnchannels(), wv.getsampwidth(), wv.getframerate(), wv.getnframes()
    raw = wv.readframes(n)
    wv.close()
    if sw == 1:
        samples = [b - 128 for b in raw]
    else:
        samples = [struct.unpack_from("<h", raw, i)[0] for i in range(0, len(raw), 2)]
    if ch == 2:
        samples = [(samples[i] + samples[i + 1]) // 2 for i in range(0, len(samples) - 1, 2)]
    if fr > SND_RATE:
        # 降采样: 每个输出样本取源窗口的平均值(盒式低通, 抑制混叠)。
        # 22050 -> 11025 时窗口正好是两个样本, 等价于两点平均。
        ratio = fr / SND_RATE
        out = []
        for k in range(int(len(samples) / ratio)):
            i0 = int(k * ratio)
            i1 = max(i0 + 1, int((k + 1) * ratio + 0.5))
            seg = samples[i0:i1]
            out.append(sum(seg) // len(seg))
        samples = out
    elif fr < SND_RATE:
        # 升采样: 线性插值
        out = []
        for k in range(int(len(samples) * SND_RATE / fr)):
            pos = k * fr / SND_RATE
            i0 = int(pos)
            i1 = min(i0 + 1, len(samples) - 1)
            f = pos - i0
            out.append(int(samples[i0] * (1 - f) + samples[i1] * f))
        samples = out
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples)
    dst.write_bytes(b"SND1" + struct.pack("<II", SND_RATE, len(samples)) + data)


def main():
    if not SRC.is_dir():
        print(f"missing {SRC}")
        return 1
    DST.mkdir(parents=True, exist_ok=True)
    count = 0
    for root, _dirs, files in os.walk(SRC):
        for fn in sorted(files):
            p = Path(root) / fn
            rel = p.relative_to(SRC).with_suffix("")
            low = fn.lower()
            if low.endswith(".png"):
                d = DST / rel.with_suffix(".img")
                d.parent.mkdir(parents=True, exist_ok=True)
                pack_png(p, d)
                count += 1
                print(f"  img  {rel}")
            elif low.endswith(".wav"):
                d = DST / rel.with_suffix(".snd")
                d.parent.mkdir(parents=True, exist_ok=True)
                pack_wav(p, d)
                count += 1
                print(f"  snd  {rel}")
    print(f"packed {count} assets -> {DST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
