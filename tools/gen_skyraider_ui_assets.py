#!/usr/bin/env python3
"""Generate original Skyraider cockpit UI assets. Standard library only."""
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets-src" / "skyraider"
PREVIEW = ROOT / "tools" / "_preview" / "skyraider_ui.png"
KEY = (255, 0, 255)
VOID = (8, 14, 26)
INK = (18, 28, 43)
STEEL = (56, 81, 105)
LITE = (157, 198, 210)
ICE = (225, 245, 238)
CYAN = (73, 221, 214)
MINT = (117, 239, 184)
GOLD = (249, 192, 67)
RED = (229, 75, 81)


def write_png(path, width, height, pixels):
    def chunk(kind, data):
        body = struct.pack(">I", len(data)) + kind + data
        return body + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    head = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def canvas(width, height, color=KEY):
    return bytearray(bytes(color) * width * height)


def px(image, width, x, y, color):
    if 0 <= x < width and 0 <= y < len(image) // (width * 3):
        index = (y * width + x) * 3
        image[index:index + 3] = bytes(color)


def rect(image, width, x, y, rect_width, rect_height, color):
    for py in range(y, y + rect_height):
        for current_x in range(x, x + rect_width):
            px(image, width, current_x, py, color)


def plate(image, width, x, y, rect_width, rect_height, accent=CYAN):
    rect(image, width, x, y, rect_width, rect_height, VOID)
    rect(image, width, x + 2, y + 2, rect_width - 4, rect_height - 4, INK)
    rect(image, width, x + 3, y + 3, rect_width - 6, 2, LITE)
    rect(image, width, x + 4, y + 6, rect_width - 8, 1, accent)
    rect(image, width, x + 3, y + rect_height - 5, rect_width - 6, 2, STEEL)


def save(name, width, height, image):
    write_png(OUT / f"{name}.png", width, height, image)


def top_frame():
    width, height = 480, 40
    image = canvas(width, height)
    rect(image, width, 0, 0, width, 3, VOID)
    rect(image, width, 0, 3, width, 2, LITE)
    rect(image, width, 0, 5, width, 2, CYAN)
    rect(image, width, 0, 7, width, 1, STEEL)
    for x in (4, 154, 326, 458):
        rect(image, width, x, 8, 18, 3, GOLD)
        rect(image, width, x + 3, 11, 12, 2, STEEL)
    for x in (0, 476):
        rect(image, width, x, 8, 4, 24, VOID)
        rect(image, width, x + (4 if x == 0 else -1), 12, 1, 16, CYAN)
    save("ui_top_frame", width, height, image)


def hud_plates():
    image = canvas(176, 29); plate(image, 176, 0, 0, 176, 29, CYAN)
    rect(image, 176, 10, 12, 8, 5, GOLD); rect(image, 176, 12, 10, 4, 9, GOLD)
    save("ui_score_plate", 176, 29, image)
    image = canvas(128, 29); plate(image, 128, 0, 0, 128, 29, RED)
    rect(image, 128, 9, 10, 7, 8, RED); rect(image, 128, 16, 8, 5, 12, RED); rect(image, 128, 21, 10, 7, 8, RED)
    save("ui_lives_plate", 128, 29, image)
    image = canvas(220, 18); plate(image, 220, 0, 0, 220, 18, GOLD)
    for x in range(14, 206, 16): rect(image, 220, x, 11, 8, 2, STEEL)
    save("ui_prompt_plate", 220, 18, image)


def indicators():
    image = canvas(20, 14)
    rect(image, 20, 1, 5, 18, 5, VOID); rect(image, 20, 3, 3, 12, 9, RED)
    rect(image, 20, 7, 1, 4, 12, RED); rect(image, 20, 4, 5, 9, 2, ICE)
    save("ui_life_pip", 20, 14, image)
    image = canvas(30, 30)
    for x, y, w, h in ((13, 0, 4, 8), (13, 22, 4, 8), (0, 13, 8, 4), (22, 13, 8, 4)):
        rect(image, 30, x, y, w, h, CYAN)
    rect(image, 30, 9, 9, 12, 12, CYAN); rect(image, 30, 12, 12, 6, 6, KEY)
    save("ui_target_lock", 30, 30, image)
    image = canvas(64, 12)
    rect(image, 64, 0, 1, 64, 10, VOID); rect(image, 64, 2, 3, 60, 6, STEEL)
    rect(image, 64, 4, 4, 40, 4, MINT); rect(image, 64, 4, 4, 40, 1, ICE)
    save("ui_shield_bar", 64, 12, image)


def overlays():
    image = canvas(180, 58)
    plate(image, 180, 0, 0, 180, 58, GOLD)
    rect(image, 180, 10, 12, 160, 2, GOLD); rect(image, 180, 10, 42, 160, 2, GOLD)
    rect(image, 180, 17, 20, 8, 16, CYAN); rect(image, 180, 155, 20, 8, 16, CYAN)
    save("ui_mission_frame", 180, 58, image)
    image = canvas(72, 18)
    plate(image, 72, 0, 0, 72, 18, RED)
    for x in (12, 29, 46): rect(image, 72, x, 6, 9, 6, RED)
    save("ui_warning_plate", 72, 18, image)


def preview():
    width, height = 480, 160
    image = canvas(width, height, (4, 8, 17))
    positions = (("ui_top_frame.png", 0, 0), ("ui_score_plate.png", 8, 48), ("ui_lives_plate.png", 196, 48),
                 ("ui_prompt_plate.png", 8, 84), ("ui_life_pip.png", 246, 84), ("ui_target_lock.png", 282, 78),
                 ("ui_shield_bar.png", 326, 87), ("ui_mission_frame.png", 8, 104), ("ui_warning_plate.png", 204, 122))
    for name, ox, oy in positions:
        data = (OUT / name).read_bytes(); asset_width, asset_height = struct.unpack(">II", data[16:24])
        raw = zlib.decompress(data[data.find(b"IDAT") + 4:-12])
        for y in range(asset_height):
            row = raw[y * (asset_width * 3 + 1) + 1:(y + 1) * (asset_width * 3 + 1)]
            for x in range(asset_width):
                color = tuple(row[x * 3:x * 3 + 3])
                if color != KEY: px(image, width, ox + x, oy + y, color)
    write_png(PREVIEW, width, height, image)


def main():
    top_frame(); hud_plates(); indicators(); overlays(); preview()
    print(f"Skyraider UI assets -> {OUT}")
    print(f"preview -> {PREVIEW}")


if __name__ == "__main__":
    main()