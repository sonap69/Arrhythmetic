"""Pixel-art metronome icon for Arrhythmetic, drawn from code so it stays editable.

Writes icon32.png (the master grid), icon16.png (hand-tuned small size),
icon256.png (32 grid scaled x8, nearest neighbour) and a preview sheet.
usage: python3 make_icon.py Resources   (needs Pillow: pip install pillow)
"""
import sys
from PIL import Image

OUT = sys.argv[1]

PAL = {
    ".": (0, 0, 0, 0),
    "K": (14, 14, 16, 255),     # outline
    "R": (217, 69, 43, 255),    # body red (the app's g1)
    "r": (163, 48, 29, 255),    # body shade
    "h": (240, 112, 84, 255),   # body highlight
    "D": (28, 29, 33, 255),     # front opening / LED panel
    "W": (236, 230, 214, 255),  # arm (cream, g4)
    "Y": (233, 197, 58, 255),   # weight (yellow, g3)
    "y": (176, 143, 30, 255),   # weight shade
    "O": (232, 137, 42, 255),   # pivot (orange, g2)
    "L": (255, 74, 46, 255),    # LED lit (the tempo display red)
    "l": (92, 30, 22, 255),     # LED off
    "G": (58, 60, 66, 255),     # base
    "g": (40, 42, 47, 255),     # base shade
}


def blank(n):
    return [["." for _ in range(n)] for _ in range(n)]


def put(img, x, y, c):
    if 0 <= y < len(img) and 0 <= x < len(img):
        img[y][x] = c


def line(x0, y0, x1, y1):
    pts, dx, dy = [], abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        pts.append((x0, y0))
        if x0 == x1 and y0 == y1:
            return pts
        e2 = 2 * err
        if e2 >= dy:
            err += dy; x0 += sx
        if e2 <= dx:
            err += dx; y0 += sy


def draw32():
    n = 32
    img = blank(n)
    top, bottom = 4, 25            # body rows
    hw_top, hw_bot = 3, 11         # half widths

    def hw(y):
        return round(hw_top + (hw_bot - hw_top) * (y - top) / (bottom - top))

    # body with outline, highlight on the left edge, shade on the right
    for y in range(top, bottom + 1):
        w = hw(y)
        x0, x1 = 16 - w, 15 + w
        for x in range(x0, x1 + 1):
            c = "R"
            if x == x0 + 1: c = "h"
            if x >= x1 - 2: c = "r"
            put(img, x, y, c)
        put(img, x0 - 1, y, "K"); put(img, x1 + 1, y, "K")
    for x in range(16 - hw_top - 1, 16 + hw_top + 1):   # top edge
        put(img, x, top - 1, "K")

    # the opening the pendulum swings in front of
    for y in range(7, 18):
        w = hw(y) - 3
        for x in range(16 - w, 16 + w):
            put(img, x, y, "D")

    # LED beat lights: beat one lit, the rest dark
    for x in range(10, 22):
        for y in (19, 20, 21, 22):
            put(img, x, y, "K" if y in (19, 22) else "D")
    for i, x in enumerate((11, 14, 17, 20)):
        for dx in (0, 1):
            put(img, x + dx, 20, "L" if i == 0 else "l")
            put(img, x + dx, 21, "L" if i == 0 else "l")

    # base plinth
    for y in (26, 27, 28):
        for x in range(3, 29):
            put(img, x, y, "g" if y == 28 else "G")
    for x in range(2, 30):
        put(img, x, 29, "K")
    for y in (26, 27, 28):
        put(img, 2, y, "K"); put(img, 29, y, "K")

    # pendulum arm from the pivot, swung to the right, sticking out above the body
    arm = line(16, 17, 23, 1)
    for x, y in arm:
        put(img, x, y, "W")
    for x, y in arm:                       # dark edge on the arm's left side where it is
        if 0 <= x - 1 and img[y][x - 1] == ".":   # outside the body, so it reads on light backgrounds
            put(img, x - 1, y, "K")
    tx, ty = arm[-1]
    put(img, tx, ty - 1, "K")   # cap the tip
    # pivot
    for x, y in ((15, 17), (16, 17), (15, 18), (16, 18)):
        put(img, x, y, "O")

    # sliding weight on the arm, with outline
    wy = 8
    wx = [x for x, y in arm if y == wy][0]
    for y in range(wy - 2, wy + 3):
        for x in range(wx - 3, wx + 3):
            put(img, x, y, "K")
    for y in range(wy - 1, wy + 2):
        for x in range(wx - 2, wx + 2):
            put(img, x, y, "y" if x == wx + 1 or y == wy + 1 else "Y")
    return img


def draw16():
    n = 16
    img = blank(n)
    top, bottom = 2, 12
    hw_top, hw_bot = 2, 7

    def hw(y):
        return round(hw_top + (hw_bot - hw_top) * (y - top) / (bottom - top))

    for y in range(top, bottom + 1):
        w = hw(y)
        x0, x1 = 8 - w, 7 + w
        for x in range(x0, x1 + 1):
            put(img, x, y, "r" if x == x1 else "R")
        put(img, x0 - 1, y, "K"); put(img, x1 + 1, y, "K")
    for x in range(8 - hw_top - 1, 8 + hw_top + 1):
        put(img, x, top - 1, "K")
    for y in range(4, 9):                  # opening
        w = hw(y) - 2
        for x in range(8 - w, 8 + w):
            put(img, x, y, "D")
    for x in range(4, 12):                 # LED strip: one beat lit
        put(img, x, 10, "D")
    put(img, 5, 10, "L"); put(img, 7, 10, "l"); put(img, 9, 10, "l")
    for x in range(1, 15):                 # base
        put(img, x, 13, "G")
    for x in range(0, 16):
        put(img, x, 14, "K")
    put(img, 0, 13, "K"); put(img, 15, 13, "K")
    arm = line(8, 9, 11, 0)
    for x, y in arm:
        put(img, x, y, "W")
    for x, y in arm:
        if 0 <= x - 1 and img[y][x - 1] == ".":
            put(img, x - 1, y, "K")
    put(img, 7, 9, "O"); put(img, 8, 9, "O")
    wy = 5                                  # weight: a 2x2 block on the arm
    wx = [x for x, y in arm if y == wy][0]
    for x, y in ((wx, wy), (wx + 1, wy), (wx, wy + 1), (wx + 1, wy + 1)):
        put(img, x, y, "Y")
    return img


def to_image(grid):
    n = len(grid)
    im = Image.new("RGBA", (n, n))
    im.putdata([PAL[c] for row in grid for c in row])
    return im


big = to_image(draw32())
small = to_image(draw16())
big.save(f"{OUT}/icon32.png")
small.save(f"{OUT}/icon16.png")
big.resize((256, 256), Image.NEAREST).save(f"{OUT}/icon256.png")

# preview: both sizes enlarged, on dark and light backgrounds
sheet = Image.new("RGBA", (1100, 560), (23, 24, 27, 255))
sheet.paste((235, 235, 235, 255), (550, 0, 1100, 560))
for ox in (0, 550):
    sheet.alpha_composite(big.resize((384, 384), Image.NEAREST), (ox + 24, 24))
    sheet.alpha_composite(small.resize((96, 96), Image.NEAREST), (ox + 430, 24))
    sheet.alpha_composite(big, (ox + 430, 160))
    sheet.alpha_composite(small, (ox + 430, 220))
    sheet.alpha_composite(big.resize((64, 64), Image.NEAREST), (ox + 430, 260))
sheet.save(f"{OUT}/preview.png")
