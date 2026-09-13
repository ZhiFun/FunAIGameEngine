#!/usr/bin/env python3
"""Generate reusable pixel UI PNGs in assets-src/common/. Standard library only."""
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets-src" / "common"
PREVIEW = ROOT / "tools" / "_preview" / "common_ui.png"
TRANSPARENT = (255, 0, 255)
INK = (14, 25, 38)
EDGE = (42, 72, 88)
PANEL = (24, 48, 62)
PANEL_LIGHT = (34, 70, 82)
CYAN = (74, 222, 215)
WHITE = (230, 247, 244)
MUTED = (102, 132, 142)
GOLD = (255, 202, 72)
RED = (239, 87, 87)
GREEN = (94, 211, 123)
VOID = (10, 16, 28)
STEEL = (55, 84, 105)
PALE = (181, 220, 218)
MINT = (126, 242, 198)
SUN = (255, 226, 124)


def write_png(path, width, height, pixels):
    def chunk(kind, data):
        body = struct.pack(">I", len(data)) + kind + data
        return body + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    rows = b"".join(b"\x00" + pixels[row * width * 3:(row + 1) * width * 3]
                    for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def canvas(width, height, fill=TRANSPARENT):
    return bytearray(bytes(fill) * width * height)


def put(image, width, x, y, color):
    height = len(image) // (width * 3)
    if 0 <= x < width and 0 <= y < height:
        offset = (y * width + x) * 3
        image[offset:offset + 3] = bytes(color)


def rect(image, width, x, y, rect_width, rect_height, color):
    for py in range(y, y + rect_height):
        for px in range(x, x + rect_width):
            put(image, width, px, py, color)


def frame(image, width, x, y, rect_width, rect_height, border, fill):
    rect(image, width, x, y, rect_width, rect_height, border)
    if rect_width > 4 and rect_height > 4:
        rect(image, width, x + 2, y + 2, rect_width - 4, rect_height - 4, fill)


def bevel(image, width, x, y, rect_width, rect_height, fill, accent=CYAN):
    frame(image, width, x, y, rect_width, rect_height, VOID, fill)
    rect(image, width, x + 2, y + 2, rect_width - 4, 2, WHITE)
    rect(image, width, x + 2, y + 2, 2, rect_height - 4, STEEL)
    rect(image, width, x + 2, y + rect_height - 4, rect_width - 4, 2, INK)
    rect(image, width, x + rect_width - 4, y + 2, 2, rect_height - 4, INK)
    rect(image, width, x + 6, y + 6, rect_width - 12, 1, accent)


def circle(image, width, cx, cy, radius, color, inner=None):
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            distance = (x - cx) ** 2 + (y - cy) ** 2
            if distance <= radius * radius and (inner is None or distance >= inner * inner):
                put(image, width, x, y, color)


def diamond(image, width, cx, cy, radius, color):
    for y in range(cy - radius, cy + radius + 1):
        span = radius - abs(y - cy)
        for x in range(cx - span, cx + span + 1):
            put(image, width, x, y, color)


def save(name, width, height, image):
    write_png(OUT / f"{name}.png", width, height, image)


def make_panel(name, width, height, accent=CYAN):
    image = canvas(width, height)
    bevel(image, width, 0, 0, width, height, PANEL, accent)
    rect(image, width, 7, 9, width - 14, height - 18, PANEL_LIGHT)
    rect(image, width, 8, 10, width - 16, 1, accent)
    for x, y in ((4, 4), (width - 6, 4), (4, height - 6), (width - 6, height - 6)):
        rect(image, width, x, y, 3, 3, accent)
    save(name, width, height, image)


def make_button(name, fill, highlight):
    width, height = 96, 32
    image = canvas(width, height)
    bevel(image, width, 0, 0, width, height, fill, highlight)
    rect(image, width, 7, 9, width - 14, 2, WHITE if fill != MUTED else EDGE)
    rect(image, width, 12, 15, width - 24, 3, PANEL if fill != MUTED else EDGE)
    rect(image, width, 7, 24, width - 14, 2, INK)
    save(name, width, height, image)


def make_dpad():
    width = height = 72
    image = canvas(width, height)
    for x, y in ((24, 4), (4, 24), (44, 24), (24, 44), (24, 24)):
        bevel(image, width, x, y, 24, 24, STEEL, PALE)
    diamond(image, width, 36, 12, 6, VOID)
    diamond(image, width, 12, 36, 6, VOID)
    diamond(image, width, 60, 36, 6, VOID)
    diamond(image, width, 36, 60, 6, VOID)
    box_color = (20, 37, 54)
    frame(image, width, 31, 31, 10, 10, VOID, box_color)
    save("control_dpad", width, height, image)


def make_action_button(name, color, glyph):
    width = height = 32
    image = canvas(width, height)
    circle(image, width, 16, 16, 15, VOID)
    circle(image, width, 16, 16, 12, color)
    circle(image, width, 13, 12, 3, SUN if color == GOLD else WHITE)
    if glyph == "a":
        rect(image, width, 12, 10, 3, 12, INK)
        rect(image, width, 18, 10, 3, 12, INK)
        rect(image, width, 13, 15, 7, 3, INK)
    else:
        rect(image, width, 11, 9, 3, 14, INK)
        rect(image, width, 14, 9, 5, 3, INK)
        rect(image, width, 14, 15, 5, 3, INK)
        rect(image, width, 14, 20, 5, 3, INK)
    save(name, width, height, image)


def make_arrow(name, direction):
    width = height = 20
    image = canvas(width, height)
    points = {
        "left": (4, 10, 1, 0), "right": (15, 10, -1, 0),
        "up": (10, 4, 0, 1), "down": (10, 15, 0, -1),
    }[direction]
    center_x, center_y, delta_x, delta_y = points
    for step in range(7):
        if delta_x:
            rect(image, width, center_x + delta_x * step, center_y - step, 1, step * 2 + 1, CYAN)
        else:
            rect(image, width, center_x - step, center_y + delta_y * step, step * 2 + 1, 1, CYAN)
    save(name, width, height, image)


def make_icons():
    image = canvas(32, 32)
    circle(image, 32, 16, 16, 14, VOID)
    circle(image, 32, 16, 16, 11, PANEL_LIGHT)
    circle(image, 32, 13, 12, 2, WHITE)
    rect(image, 32, 10, 9, 4, 14, CYAN)
    rect(image, 32, 18, 9, 4, 14, CYAN)
    save("icon_pause", 32, 32, image)

    image = canvas(32, 32)
    circle(image, 32, 16, 16, 14, VOID)
    circle(image, 32, 16, 16, 11, PANEL_LIGHT)
    for y in range(9, 24):
        for x in range(10, 23):
            if x - 10 <= (y - 9) // 2 and x - 10 <= (23 - y) // 2:
                put(image, 32, x, y, MINT)
    save("icon_play", 32, 32, image)

    image = canvas(32, 32)
    circle(image, 32, 16, 16, 14, VOID)
    circle(image, 32, 16, 16, 11, PANEL_LIGHT)
    rect(image, 32, 10, 10, 12, 12, RED)
    save("icon_stop", 32, 32, image)

    image = canvas(32, 32)
    circle(image, 32, 16, 16, 13, VOID)
    circle(image, 32, 16, 16, 10, PANEL_LIGHT)
    for angle in range(0, 360, 45):
        x = round(16 + math.cos(math.radians(angle)) * 10)
        y = round(16 + math.sin(math.radians(angle)) * 10)
        rect(image, 32, x - 2, y - 2, 5, 5, CYAN)
    circle(image, 32, 16, 16, 5, INK)
    save("icon_settings", 32, 32, image)


def make_status_assets():
    image = canvas(24, 22)
    circle(image, 24, 7, 8, 6, RED)
    circle(image, 24, 17, 8, 6, RED)
    diamond(image, 24, 12, 14, 9, RED)
    save("status_heart", 24, 22, image)

    image = canvas(24, 24)
    circle(image, 24, 12, 12, 10, INK)
    circle(image, 24, 12, 12, 8, GOLD)
    circle(image, 24, 9, 9, 3, (255, 236, 145))
    save("status_coin", 24, 24, image)

    image = canvas(24, 24)
    diamond(image, 24, 12, 12, 10, INK)
    diamond(image, 24, 12, 12, 8, CYAN)
    diamond(image, 24, 9, 9, 3, WHITE)
    save("status_gem", 24, 24, image)

    for name, color, amount in (("bar_health", GREEN, 76), ("bar_energy", CYAN, 60)):
        image = canvas(96, 12)
        frame(image, 96, 0, 0, 96, 12, INK, EDGE)
        rect(image, 96, 3, 3, amount, 6, color)
        save(name, 96, 12, image)


def make_overlays():
    image = canvas(32, 32)
    circle(image, 32, 16, 16, 12, CYAN, 9)
    rect(image, 32, 15, 2, 2, 28, CYAN)
    rect(image, 32, 2, 15, 28, 2, CYAN)
    save("overlay_crosshair", 32, 32, image)

    image = canvas(18, 24)
    for step in range(12):
        rect(image, 18, 1 + step, step * 2, 2, 2, WHITE)
    rect(image, 18, 2, 2, 2, 15, CYAN)
    save("overlay_cursor", 18, 24, image)

    image = canvas(48, 48)
    for x, y in ((0, 0), (40, 0), (0, 40), (40, 40)):
        rect(image, 48, x, y, 8, 3, CYAN)
        rect(image, 48, x, y, 3, 8, CYAN)
    save("overlay_select", 48, 48, image)

    image = canvas(32, 32)
    for index in range(8):
        angle = math.radians(index * 45 - 90)
        x = round(16 + math.cos(angle) * 11)
        y = round(16 + math.sin(angle) * 11)
        color = WHITE if index < 3 else MUTED
        circle(image, 32, x, y, 3, color)
    save("overlay_spinner", 32, 32, image)


def make_preview():
    width, height = 480, 240
    image = canvas(width, height, (8, 17, 27))
    files = sorted(OUT.glob("*.png"))
    for index, path in enumerate(files):
        data = path.read_bytes()
        # Preview uses the source image payload layout written by write_png; rendering labels is deliberately avoided.
        column, row = index % 8, index // 8
        x0, y0 = 8 + column * 59, 8 + row * 58
        raw = zlib.decompress(data[data.find(b"IDAT") + 4:-12])
        # The source PNG rows are filter byte + RGB triples; dimensions come from its IHDR.
        asset_width, asset_height = struct.unpack(">II", data[16:24])
        for y in range(min(asset_height, 48)):
            row_data = raw[y * (asset_width * 3 + 1) + 1:(y + 1) * (asset_width * 3 + 1)]
            for x in range(min(asset_width, 55)):
                color = tuple(row_data[x * 3:x * 3 + 3])
                if color != TRANSPARENT:
                    put(image, width, x0 + x, y0 + y, color)
    write_png(PREVIEW, width, height, image)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    make_panel("panel", 64, 48)
    make_panel("dialog", 160, 72, GOLD)
    make_button("button_normal", PANEL_LIGHT, CYAN)
    make_button("button_pressed", (20, 92, 96), WHITE)
    make_button("button_disabled", MUTED, EDGE)
    make_dpad()
    make_action_button("control_a", RED, "a")
    make_action_button("control_b", GOLD, "b")
    for direction in ("left", "right", "up", "down"):
        make_arrow(f"arrow_{direction}", direction)
    make_icons()
    make_status_assets()
    make_overlays()
    make_preview()
    print(f"common UI assets -> {OUT}")
    print(f"preview -> {PREVIEW}")


if __name__ == "__main__":
    main()