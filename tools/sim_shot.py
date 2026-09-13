#!/usr/bin/env python3
"""跑模拟器并抓一张窗口截图(移植/验收一直要用)。

  py -3 tools\\sim_shot.py out.png [游戏名] [--zoom N] [--wait 秒]

抓图用 PrintWindow(窗口自身内容), **不受遮挡影响** ——
用 ImageGrab 抓屏幕的话, 前台被别的程序(编辑器/播放器)盖住就抓到别人了, 踩过。

⚠ 游戏名只传 id(如 vmtest), 不要带 games-bin\\ 和 .gbn —— find_cart 会自己拼。
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

from PIL import Image, ImageGrab

ROOT = Path(__file__).resolve().parent.parent

# 模拟器是 MinGW 链接的 Qt 程序, 直接起会以 0xC0000135(STATUS_DLL_NOT_FOUND) 退出:
# 找不到 Qt6Core.dll。必须把 Qt 和 MinGW 的 bin 放进 PATH。
QT_BINS = [r"C:\Qt\6.9.3\mingw_64\bin", r"C:\Qt\Tools\mingw1310_64\bin"]

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.SetProcessDPIAware()

SW_RESTORE = 9
HWND_TOPMOST = -1
SWP_NOMOVE, SWP_NOSIZE, SWP_SHOWWINDOW = 0x0002, 0x0001, 0x0040
PW_RENDERFULLCONTENT = 2


class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG),
                ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


def find_exes():
    return sorted(ROOT.glob("build/**/fun_sim.exe"))


def windows_of(pid=None):
    """枚举顶层可见窗口; pid 非空时只找该进程的"""
    out = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        if pid is not None:
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value != pid:
                return True
        r = RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        if r.right - r.left < 100 or r.bottom - r.top < 100:
            return True
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        out.append((hwnd, buf.value, r))
        return True

    user32.EnumWindows(cb, 0)
    return out


def capture_window(hwnd, r):
    """PrintWindow: 拿窗口自己的内容, 不管它被谁盖着"""
    w, h = r.right - r.left, r.bottom - r.top
    hdc = user32.GetWindowDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mdc, bmp)
    ok = user32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT)

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h                 # 负高度 = 行序从上到下
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    got = gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)

    gdi32.SelectObject(mdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    if not got:
        return None
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB"), bool(ok)


def raise_window(hwnd):
    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("game", nargs="?", default="vmtest")
    ap.add_argument("--zoom", default="2")
    ap.add_argument("--wait", type=float, default=6.0)
    ap.add_argument("--exe", help="指定用哪个 fun_sim.exe")
    ap.add_argument("--fallback-grab", action="store_true", help="PrintWindow 失败时退回抓屏")
    a = ap.parse_args()

    cands = find_exes()
    for c in cands:
        print(f"  候选: {c}  {c.stat().st_size}B  "
              f"{time.strftime('%m-%d %H:%M', time.localtime(c.stat().st_mtime))}")
    if a.exe:
        exe = Path(a.exe)
    elif cands:
        exe = max(cands, key=lambda c: c.stat().st_mtime)
    else:
        print("找不到 fun_sim.exe, 先跑 tools\\_build.cmd")
        return 1
    print(f"exe: {exe}")
    print(f"游戏: {a.game!r}  (只传游戏名, 不要带 games-bin\\ 和 .gbn)")

    logf = open(ROOT / "_sim_run.log", "wb")
    env = os.environ.copy()
    env["PATH"] = ";".join([p for p in QT_BINS if Path(p).is_dir()] + [env.get("PATH", "")])
    proc = subprocess.Popen([str(exe), a.game, "--zoom", a.zoom], cwd=str(ROOT),
                            stdout=logf, stderr=subprocess.STDOUT, env=env)
    time.sleep(a.wait)

    alive = proc.poll() is None
    print("进程存活: " + ("是" if alive else
                         f"否 (退出码 {proc.returncode}, 0x{proc.returncode & 0xFFFFFFFF:X})"))
    wins = windows_of(pid=proc.pid)
    if not wins:
        print("该进程没有可见窗口; 顺便列出当前有标题的窗口:")
        for hwnd, title, r in windows_of():
            if title:
                print(f"  {r.right - r.left}x{r.bottom - r.top}  {title!r}")

    logf.close()
    txt = (ROOT / "_sim_run.log").read_text(encoding="utf-8", errors="replace").strip()
    if txt:
        print("--- 模拟器输出 ---")
        print(txt[:3000])

    if not wins:
        proc.terminate()
        return 1

    hwnd, title, r = max(wins, key=lambda t: (t[2].right - t[2].left) * (t[2].bottom - t[2].top))
    print(f"窗口: {title!r} {r.right - r.left}x{r.bottom - r.top}")
    raise_window(hwnd)

    shot = capture_window(hwnd, r)
    if shot is None and a.fallback_grab:
        print("PrintWindow 失败, 退回抓屏(可能抓到被遮挡的别的窗口)")
        shot = (ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom)), False)
    if shot is None:
        proc.terminate()
        return 1
    img, ok = shot
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    img.save(a.out)
    print(f"-> {a.out}  {img.size}  (PrintWindow={ok})")

    user32.SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
