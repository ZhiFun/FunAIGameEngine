#!/usr/bin/env python3
"""脚本 VM 的参考实现(纯 Python) —— 用来在没有 Qt/硬件的环境下跑 .gbn。

它的作用是**验证与快速迭代**, 不是给人玩: 字节码与 engine/src/ScriptGame.cpp 一一对应,
显示指令画进一块软件帧缓冲(RGB565), 可选导出 PNG 看画面对不对。

  py -3 tools\\gs_run.py games-src\\vmtest\\game.gs                # 直接编译 + 跑 60 帧
  py -3 tools\\gs_run.py games-bin\\vmtest.gbn --frames 120
  py -3 tools\\gs_run.py games-bin\\vmtest.gbn --png out.png --frames 30
  py -3 tools\\gs_run.py games-bin\\vmtest.gbn --globals cnt,pos

⚠ 两份实现必须同步: 改了 ScriptGame.cpp 的指令语义, 这里要跟着改。
"""
import argparse
import re
import struct
import sys
from pathlib import Path

# 控制台通常是 GBK, 输出里带非 ASCII 会直接抛 UnicodeEncodeError
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gs_compiler                      # noqa: E402

# --- 指令集(必须与 ScriptGame.cpp 一致) ---
OP_HALT, OP_PUSH, OP_LOADG, OP_STOREG, OP_POP = 0x00, 0x01, 0x02, 0x03, 0x04
OP_LDMEM, OP_STMEM, OP_LDIDX, OP_STIDX = 0x05, 0x06, 0x07, 0x08
OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD = 0x10, 0x11, 0x12, 0x13, 0x14
OP_NEG, OP_LNOT = 0x15, 0x16
OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE = 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C
OP_LAND, OP_LOR = 0x1D, 0x1E
OP_AND = 0x1F
OP_JMP, OP_JZ, OP_JNZ = 0x20, 0x21, 0x22
OP_OR, OP_XOR, OP_SHL, OP_SHR = 0x23, 0x24, 0x25, 0x26
OP_CALLN = 0x30
OP_CALL_USER, OP_RET = 0x31, 0x32
OP_IMG, OP_TEXT, OP_SFX, OP_BGM = 0x40, 0x41, 0x42, 0x43
OP_IMGF, OP_IMGV, OP_IMGW, OP_IMGH = 0x44, 0x45, 0x46, 0x47
OP_SCHR = 0x48
OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, OP_FNEG = 0x50, 0x51, 0x52, 0x53, 0x54
OP_FLT, OP_FLE, OP_FGT, OP_FGE, OP_FEQ, OP_FNE = 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A
OP_ITOF, OP_FTOI, OP_FLAND, OP_FLOR, OP_FLNOT = 0x5B, 0x5C, 0x5D, 0x5E, 0x5F

FN_HELD, FN_PRESSED, FN_RELEASED, FN_RND, FN_RGB = 0, 1, 2, 3, 4
FN_DT_MS, FN_FRAME, FN_EXIT, FN_CLEAR, FN_PX = 5, 6, 7, 8, 9
FN_RECT, FN_FRECT, FN_SCREEN_W, FN_SCREEN_H, FN_TEXTNUM = 10, 11, 12, 13, 14
FN_ABS, FN_IMIN, FN_IMAX = 15, 16, 17
FN_ITOF, FN_FTOI, FN_FABS, FN_DTF = 18, 19, 20, 21
FN_FMIN, FN_FMAX, FN_FSQRT = 22, 23, 24

BUTTON_NAMES = ["UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "D",
                "START", "SELECT", "L", "R", "X", "Y"]

I32_MIN, I32_MAX = -2147483648, 2147483647


def s32(v):
    """模拟 C 的 int32 溢出/回绕"""
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v


def to_f(bits):
    return struct.unpack("<f", struct.pack("<i", s32(bits)))[0]


def to_bits(f):
    return struct.unpack("<i", struct.pack("<f", f))[0]


def f2i(f):
    if f != f:
        return 0
    return int(max(-2147483000, min(2147483000, f)))


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


MAGENTA = rgb565(248, 0, 248)


# ---------------------------------------------------------------------------
# 内置 5x7 字库
# 直接从 Display.cpp 里解析出来 —— 手抄一份到 Python 一定会跟 C++ 漂移。
# ---------------------------------------------------------------------------
def load_font():
    src = (ROOT / "engine" / "src" / "Display.cpp").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"FONT5x7\[64\]\[5\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if m is None:
        return None
    table = []
    for row in re.findall(r"\{([^}]*)\}", m.group(1)):
        vals = [int(v.strip(), 0) for v in row.split(",") if v.strip()]
        if len(vals) == 5:
            table.append(vals)
    return table if len(table) == 64 else None


FONT = load_font()


# ---------------------------------------------------------------------------
# .gbn 读取
# ---------------------------------------------------------------------------
class Cart:
    def __init__(self, blob: bytes):
        if blob[:4] != b"GBN1":
            raise ValueError("不是 .gbn")
        ver, self.globals, self.init, self.start, self.update, self.render = \
            struct.unpack_from("<HHIIII", blob, 4)
        code_len, nstr, nasset = struct.unpack_from("<III", blob, 24)
        self.code = blob[36:36 + code_len]
        p = 36 + code_len
        self.strings = []
        for _ in range(nstr):
            (ln,) = struct.unpack_from("<H", blob, p)
            p += 2
            self.strings.append(blob[p:p + ln].decode("utf-8"))
            p += ln
        self.assets = {}
        for _ in range(nasset):
            (ln,) = struct.unpack_from("<H", blob, p)
            p += 2
            name = blob[p:p + ln].decode("utf-8")
            p += ln
            (sz,) = struct.unpack_from("<I", blob, p)
            p += 4
            self.assets[name] = blob[p:p + sz]
            p += sz
        self.version = ver


# ---------------------------------------------------------------------------
# 软件帧缓冲(只为看清画面, 不追求性能)
# ---------------------------------------------------------------------------
class Framebuffer:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.buf = [0] * (w * h)

    def clear(self, c):
        self.buf = [c] * (self.w * self.h)

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.buf[y * self.w + x] = c

    def frect(self, x, y, w, h, c):
        for yy in range(max(0, y), min(self.h, y + h)):
            row = yy * self.w
            for xx in range(max(0, x), min(self.w, x + w)):
                self.buf[row + xx] = c

    def rect(self, x, y, w, h, c):
        self.frect(x, y, w, 1, c)
        self.frect(x, y + h - 1, w, 1, c)
        self.frect(x, y, 1, h, c)
        self.frect(x + w - 1, y, 1, h, c)

    def blit(self, img: "PackedImage", x, y, flip=False, key=MAGENTA):
        for yy in range(img.h):
            sy = y + yy
            if not (0 <= sy < self.h):
                continue
            row = sy * self.w
            for xx in range(img.w):
                c = img.pixel(xx, yy)
                if c is None or c == key:
                    continue
                dx = x + (img.w - 1 - xx if flip else xx)
                if 0 <= dx < self.w:
                    self.buf[row + dx] = c

    def text(self, x, y, s, c, scale=1):
        """与 Display::text 一致: 5x7 字模, 小写当大写, 每字 6*scale 宽"""
        if FONT is None:
            self.frect(x, y, 6 * len(s) * scale, 8 * scale, c)   # 退化成色块
            return
        scale = max(1, scale)
        cx = x
        for ch in s:
            if ch == "\n":
                cx, y = x, y + 8 * scale
                continue
            up = ch.upper()
            gi = ord(up) - 0x20 if 0x20 <= ord(up) <= 0x5F else 0
            g = FONT[gi]
            for col in range(5):
                bits = g[col]
                for row in range(7):
                    if bits & (1 << row):
                        self.frect(cx + col * scale, y + row * scale, scale, scale, c)
            cx += 6 * scale

    def save_png(self, path):
        try:
            from PIL import Image
        except ImportError:
            print("  (没装 Pillow, 跳过 PNG 导出)")
            return False
        out = Image.new("RGB", (self.w, self.h))
        px = out.load()
        for y in range(self.h):
            for x in range(self.w):
                c = self.buf[y * self.w + x]
                px[x, y] = (((c >> 11) & 31) * 255 // 31,
                            ((c >> 5) & 63) * 255 // 63,
                            (c & 31) * 255 // 31)
        out.save(path)
        return True


class PackedImage:
    """解析 .img: IMG1 = RGB565 / IMG2 = 4bpp 索引"""

    def __init__(self, blob: bytes):
        magic = blob[:4]
        self.w, self.h = struct.unpack_from("<HH", blob, 4)
        self.pal = None
        if magic == b"IMG2":
            self.pal = list(struct.unpack_from("<16H", blob, 8))
            self.data = blob[40:]
        elif magic == b"IMG1":
            self.data = blob[8:]
        else:
            raise ValueError(f"未知图片格式 {magic!r}")

    def pixel(self, x, y):
        i = y * self.w + x
        if self.pal is not None:
            b = self.data[i >> 1]
            v = (b & 0x0F) if (i & 1) else (b >> 4)
            return None if v == 0 else self.pal[v]
        p = i * 2
        c = self.data[p] | (self.data[p + 1] << 8)
        return None if c == MAGENTA else c


# ---------------------------------------------------------------------------
# VM
# ---------------------------------------------------------------------------
class Vm:
    def __init__(self, cart: Cart, w=428, h=142, pad=None, trace=False):
        self.cart = cart
        self.mem = [0] * max(cart.globals, 1)
        self.fb = Framebuffer(w, h)
        self.img_cache = {}
        self.pad = pad or set()          # 当前按住的按钮名字集合
        self.pressed_prev = set()
        self.frame = 0
        self.dt_s = 1.0 / 30.0
        self.quit = False
        self.trace = trace
        self.calls = {}                  # 统计绘制/音频调用次数
        self.bad = []                    # 运行期异常(越界/栈问题)

    # --- 资源 ---
    def image(self, name):
        if name not in self.cart.assets:
            return None
        if name not in self.img_cache:
            self.img_cache[name] = PackedImage(self.cart.assets[name])
        return self.img_cache[name]

    @staticmethod
    def asset_name(raw, ext):
        if raw.endswith("." + ext):
            return raw
        return f"{raw}.{ext}"

    # --- 主循环 ---
    def run_section(self, pc):
        if pc in (None, 0xFFFFFFFF):
            return
        code, size = self.cart.code, len(self.cart.code)
        mem = self.mem
        nglob = len(mem)
        st = []
        callstack = []                      # 只存返回地址(与 C++ VM 一致)

        def push(v):
            if len(st) < 256:
                st.append(v)

        def pop():
            return st.pop() if st else 0

        while True:
            if pc >= size:
                return
            op = code[pc]
            pc += 1
            if op == OP_HALT:
                return
            elif op == OP_PUSH:
                v, = struct.unpack_from("<i", code, pc)
                pc += 4
                push(v)
            elif op == OP_LOADG:
                i = code[pc]
                pc += 1
                push(mem[i] if i < nglob else 0)
            elif op == OP_STOREG:
                i = code[pc]
                pc += 1
                v = pop()
                if i < nglob:
                    mem[i] = s32(v)
            elif op == OP_POP:
                pop()
            elif op == OP_LDMEM:
                a, = struct.unpack_from("<H", code, pc)
                pc += 2
                push(mem[a] if a < nglob else 0)
            elif op == OP_STMEM:
                a, = struct.unpack_from("<H", code, pc)
                pc += 2
                v = pop()
                if a < nglob:
                    mem[a] = s32(v)
            elif op == OP_LDIDX:
                a, = struct.unpack_from("<H", code, pc)
                pc += 2
                i = pop()
                push(mem[a + i] if 0 <= i and a + i < nglob else 0)
            elif op == OP_STIDX:
                a, = struct.unpack_from("<H", code, pc)
                pc += 2
                i, v = pop(), pop()
                if 0 <= i and a + i < nglob:
                    mem[a + i] = s32(v)
            elif op in (OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_LT, OP_LE, OP_GT,
                        OP_GE, OP_EQ, OP_NE, OP_LAND, OP_LOR, OP_AND, OP_OR, OP_XOR,
                        OP_SHL, OP_SHR):
                b, a2 = pop(), st[-1]
                if op == OP_ADD:
                    r = a2 + b
                elif op == OP_SUB:
                    r = a2 - b
                elif op == OP_MUL:
                    r = a2 * b
                elif op == OP_DIV:
                    r = 0 if b == 0 else int(a2 / b) if a2 * b < 0 else a2 // b
                elif op == OP_MOD:
                    r = 0 if b == 0 else (abs(a2) % abs(b)) * (1 if a2 * b >= 0 else -1)
                elif op == OP_LT:
                    r = 1 if a2 < b else 0
                elif op == OP_LE:
                    r = 1 if a2 <= b else 0
                elif op == OP_GT:
                    r = 1 if a2 > b else 0
                elif op == OP_GE:
                    r = 1 if a2 >= b else 0
                elif op == OP_EQ:
                    r = 1 if a2 == b else 0
                elif op == OP_NE:
                    r = 1 if a2 != b else 0
                elif op == OP_LAND:
                    r = 1 if (a2 != 0 and b != 0) else 0
                elif op == OP_LOR:
                    r = 1 if (a2 != 0 or b != 0) else 0
                elif op == OP_AND:
                    r = a2 & b
                elif op == OP_OR:
                    r = a2 | b
                elif op == OP_XOR:
                    r = a2 ^ b
                elif op == OP_SHL:
                    r = s32((a2 & 0xFFFFFFFF) << (b & 31))
                else:
                    r = a2 >> (b & 31)
                st[-1] = s32(r)
            elif op == OP_NEG:
                st[-1] = s32(-st[-1])
            elif op == OP_LNOT:
                st[-1] = 1 if st[-1] == 0 else 0
            elif op in (OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, OP_FLT, OP_FLE, OP_FGT,
                        OP_FGE, OP_FEQ, OP_FNE, OP_FLAND, OP_FLOR):
                b, a2 = to_f(pop()), to_f(st[-1])
                if op == OP_FADD:
                    r = a2 + b
                elif op == OP_FSUB:
                    r = a2 - b
                elif op == OP_FMUL:
                    r = a2 * b
                elif op == OP_FDIV:
                    r = 0.0 if b == 0.0 else a2 / b
                else:
                    r = None
                if r is None:
                    if op == OP_FLT:
                        r = 1 if a2 < b else 0
                    elif op == OP_FLE:
                        r = 1 if a2 <= b else 0
                    elif op == OP_FGT:
                        r = 1 if a2 > b else 0
                    elif op == OP_FGE:
                        r = 1 if a2 >= b else 0
                    elif op == OP_FEQ:
                        r = 1 if a2 == b else 0
                    elif op == OP_FNE:
                        r = 1 if a2 != b else 0
                    elif op == OP_FLAND:
                        r = 1 if (a2 != 0.0 and b != 0.0) else 0
                    else:
                        r = 1 if (a2 != 0.0 or b != 0.0) else 0
                    st[-1] = s32(r)
                else:
                    st[-1] = to_bits(r)
            elif op == OP_FNEG:
                st[-1] = s32(st[-1] ^ -2147483648)
            elif op == OP_FLNOT:
                st[-1] = 1 if to_f(st[-1]) == 0.0 else 0
            elif op == OP_ITOF:
                st[-1] = to_bits(float(st[-1]))
            elif op == OP_FTOI:
                st[-1] = s32(f2i(to_f(st[-1])))
            elif op == OP_JMP:
                pc, = struct.unpack_from("<H", code, pc)
            elif op == OP_JZ:
                a, = struct.unpack_from("<H", code, pc)
                pc = a if pop() == 0 else pc + 2
            elif op == OP_JNZ:
                a, = struct.unpack_from("<H", code, pc)
                pc = a if pop() != 0 else pc + 2
            elif op == OP_CALLN:
                fn, argc = code[pc], code[pc + 1]
                pc += 2
                args = [0] * argc
                for i in range(argc - 1, -1, -1):
                    args[i] = pop()
                push(self.builtin(fn, args, argc))
            elif op == OP_CALL_USER:
                (target,) = struct.unpack_from("<H", code, pc)
                if len(callstack) >= 64:
                    self.bad.append("函数调用嵌套过深(脚本不支持递归)")
                    return
                callstack.append(pc + 2)
                pc = target
            elif op == OP_RET:
                if not callstack:
                    self.bad.append("RET 没有对应的 CALL")
                    return
                pc = callstack.pop()
            elif op in (OP_IMG, OP_IMGF, OP_IMGV):
                flip_idx = None
                if op == OP_IMG:
                    (sid,) = struct.unpack_from("<H", code, pc)
                    pc += 2
                elif op == OP_IMGF:
                    (sid,) = struct.unpack_from("<H", code, pc)
                    pc += 2
                    flip_idx = 1
                else:
                    base, cnt = struct.unpack_from("<HH", code, pc)
                    sid = None
                    pc += 4
                    flip_idx = 2
                x, y = pop(), pop()
                if op == OP_IMGV:
                    idx = pop()
                    sid = base + idx if 0 <= idx < cnt else None
                flip = False
                if flip_idx is not None:
                    flip = pop() != 0
                if sid is not None and sid < len(self.cart.strings):
                    nm = self.asset_name(self.cart.strings[sid], "img")
                    im = self.image(nm)
                    self.calls["img"] = self.calls.get("img", 0) + 1
                    if im is not None:
                        self.fb.blit(im, x, y, flip)
            elif op in (OP_IMGW, OP_IMGH):
                (sid,) = struct.unpack_from("<H", code, pc)
                pc += 2
                im = self.image(self.asset_name(self.cart.strings[sid], "img"))
                push((im.w if op == OP_IMGW else im.h) if im else 0)
            elif op == OP_SCHR:
                base, cnt = struct.unpack_from("<HH", code, pc)
                pc += 4
                pos, idx = pop(), pop()
                code_ch = 0
                if 0 <= idx < cnt and pos >= 0:
                    s = self.cart.strings[base + idx]
                    if pos < len(s):
                        code_ch = ord(s[pos])
                push(code_ch)
            elif op == OP_TEXT:
                (sid,) = struct.unpack_from("<H", code, pc)
                scale = code[pc + 2]
                pc += 3
                x, y, c = pop(), pop(), pop()
                self.calls["text"] = self.calls.get("text", 0) + 1
                self.fb.text(x, y, self.cart.strings[sid], c & 0xFFFF, scale or 1)
            elif op in (OP_SFX, OP_BGM):
                (sid,) = struct.unpack_from("<H", code, pc)
                pc += 2
                key = "sfx" if op == OP_SFX else "bgm"
                nm = self.asset_name(self.cart.strings[sid], "snd")
                if nm in self.cart.assets:
                    self.calls[key] = self.calls.get(key, 0) + 1
                else:
                    self.bad.append(f"缺素材: {nm}")
            else:
                self.bad.append(f"未知指令 0x{op:02X} @ {pc - 1}")
                return

    def builtin(self, fn, a, argc):
        def g(i):
            return a[i] if i < argc else 0

        if fn == FN_HELD:
            return 1 if BUTTON_NAMES[g(0) & 15] in self.pad else 0
        if fn == FN_PRESSED:
            n = BUTTON_NAMES[g(0) & 15]
            return 1 if (n in self.pad and n not in self.pressed_prev) else 0
        if fn == FN_RELEASED:
            n = BUTTON_NAMES[g(0) & 15]
            return 1 if (n not in self.pad and n in self.pressed_prev) else 0
        if fn == FN_RND:
            import random
            return random.randrange(g(0)) if g(0) > 0 else 0
        if fn == FN_RGB:
            return rgb565(g(0) & 255, g(1) & 255, g(2) & 255)
        if fn == FN_DT_MS:
            return int(self.dt_s * 1000)
        if fn == FN_FRAME:
            return self.frame
        if fn == FN_EXIT:
            self.quit = True
            return 0
        if fn == FN_CLEAR:
            self.fb.clear(g(0) & 0xFFFF)
            return 0
        if fn == FN_PX:
            self.fb.px(g(0), g(1), g(2) & 0xFFFF)
            return 0
        if fn == FN_RECT:
            self.fb.rect(g(0), g(1), g(2), g(3), g(4) & 0xFFFF)
            return 0
        if fn == FN_FRECT:
            self.fb.frect(g(0), g(1), g(2), g(3), g(4) & 0xFFFF)
            return 0
        if fn == FN_SCREEN_W:
            return self.fb.w
        if fn == FN_SCREEN_H:
            return self.fb.h
        if fn == FN_TEXTNUM:
            self.calls["textnum"] = self.calls.get("textnum", 0) + 1
            self.fb.text(g(0), g(1), str(g(2)), g(3) & 0xFFFF, max(1, g(4)))
            return 0
        if fn == FN_ABS:
            return abs(g(0))
        if fn == FN_IMIN:
            return min(g(0), g(1))
        if fn == FN_IMAX:
            return max(g(0), g(1))
        if fn == FN_ITOF:
            return to_bits(float(g(0)))
        if fn == FN_FTOI:
            return f2i(to_f(g(0)))
        if fn == FN_FABS:
            return to_bits(abs(to_f(g(0))))
        if fn == FN_DTF:
            return to_bits(self.dt_s)
        if fn == FN_FMIN:
            return to_bits(min(to_f(g(0)), to_f(g(1))))
        if fn == FN_FMAX:
            return to_bits(max(to_f(g(0)), to_f(g(1))))
        if fn == FN_FSQRT:
            v = to_f(g(0))
            return to_bits(v ** 0.5 if v > 0.0 else 0.0)
        self.bad.append(f"未知内置函数 fn{fn}")
        return 0

    def step(self, pad=None):
        """跑一帧 update + render; pad 为 None 表示这一帧不传(维持原样)"""
        if pad is not None:
            self.pressed_prev = set(self.pad)
            self.pad = set(pad)
        self.frame += 1
        self.run_section(self.cart.update)
        self.run_section(self.cart.render)


def main():
    ap = argparse.ArgumentParser(prog="gs_run", description="脚本 VM 参考实现")
    ap.add_argument("target", help="game.gs 或 .gbn")
    ap.add_argument("--frames", type=int, default=60, help="跑多少帧(默认 60)")
    ap.add_argument("--w", type=int, default=428)
    ap.add_argument("--h", type=int, default=142)
    ap.add_argument("--pad", default="", help="前一半帧按住的按钮, 逗号分隔(如 right,right,a)")
    ap.add_argument("--png", help="把最后一帧导出成 PNG")
    ap.add_argument("--globals", help="打印这些变量(逗号分隔), 默认全部")
    ap.add_argument("--arrays", help="打印这些数组(逗号分隔), 每个最多 12 项")
    ap.add_argument("--assets", action="store_true", help="列出卡带里的素材尺寸/格式")
    a = ap.parse_args()

    path = Path(a.target)
    gen = None
    if path.suffix == ".gs":
        import importlib
        importlib.reload(gs_compiler)
        try:
            code, entries, strings, gen = gs_compiler.compile_source(
                path.read_text(encoding="utf-8"))
        except gs_compiler.CompileError as exc:
            print(f"编译失败: {exc}")
            return 1
        # 内存里现搭一个包: 直接借 build_game 的打包逻辑
        import build_game
        importlib.reload(build_game)
        tmp = ROOT / "games-bin" / "_vmtest.gbn"
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = build_game.build(path.parent.name, ROOT / "games-bin", quiet=True)
        if rc != 0:
            print(buf.getvalue())
            return 1
        path = ROOT / "games-bin" / f"{path.parent.name}.gbn"
    cart = Cart(path.read_bytes())
    print(f"包 {path.name}: 版本 {cart.version}, 内存字 {cart.globals}, "
          f"字节码 {len(cart.code)}B, 字符串 {len(cart.strings)}, 素材 {len(cart.assets)}")

    if a.assets:
        for nm in sorted(cart.assets):
            blob = cart.assets[nm]
            if nm.endswith(".img"):
                try:
                    im = PackedImage(blob)
                    fmt = "IMG2/4bpp" if im.pal is not None else "IMG1/565"
                    print(f"  {nm:28s} {im.w:3d}x{im.h:<3d} {fmt}")
                except ValueError as exc:
                    print(f"  {nm:28s} {exc}")
            else:
                print(f"  {nm:28s} {len(blob)}B")
        return 0

    vm = Vm(cart, a.w, a.h)
    vm.run_section(cart.init)
    vm.run_section(cart.start)

    script = [s.strip().upper() for s in a.pad.split(",") if s.strip()]
    hold_until = a.frames // 2          # 前一半按住, 后一半松开
    for i in range(a.frames):
        vm.step(pad=script if i < hold_until else [])   # 注意: 松开要传空表, 不能传 None
        if vm.quit:
            print(f"  第 {i + 1} 帧脚本请求退出")
            break

    # 变量值
    if gen is not None:
        names = {v: k for k, v in gen.scalars.items()}
        want = a.globals.split(",") if a.globals else None
        out = []
        for idx in sorted(names):
            nm = names[idx]
            if want and nm not in want:
                continue
            v = vm.mem[idx]
            vartype = gen.vartype[nm]
            out.append(f"{nm}={to_f(v):.4f}" if vartype == "f" else f"{nm}={v}")
        print("  标量: " + ", ".join(out))

        if a.arrays:
            for nm in [s.strip() for s in a.arrays.split(",") if s.strip()]:
                sym = gen.arrays.get(nm)
                if sym is None:
                    print(f"  (没有数组 {nm})")
                    continue
                base, count = sym
                vals = [vm.mem[base + k] for k in range(min(count, 12))]
                if gen.vartype.get(nm) == "f":
                    body = ", ".join(f"{to_f(v):.2f}" for v in vals)
                else:
                    body = ", ".join(str(v) for v in vals)
                print(f"  {nm}[{count}] = {body}")

    print(f"  调用统计: {vm.calls}")
    if vm.bad:
        print("  [!] 问题:")
        for b in vm.bad[:10]:
            print(f"     {b}")
    else:
        print("  [OK] 无异常")
    if a.png:
        vm.fb.save_png(a.png)
        print(f"  -> {a.png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
