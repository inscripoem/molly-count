#!/usr/bin/env python3
"""Generates molly-count display assets.

Outputs (into ../assets/ relative to this file):
  background.2bpp  200x200 4-color packed framebuffer, everything static
                   (header text, 第/天 frame, heart, jasmine flower, bottom heart)
  digits.1bpp      10 digit glyphs, fixed 28x56 boxes, 1bpp masks (drawn in red)
  preview.png      3x upscaled composite with a sample day number

Layout (200x200): header 我们在一起 (top), red heart (top-left),
第 <day digits> 天 (middle, digits right-aligned), jasmine (bottom-right),
two runtime date strings sit at the bottom row (y 182..194).
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W = H = 200
S = 4  # supersample factor
FONT = "/nix/store/dllg6pqphyxipzwihzb6phmjlq4r0w4b-noto-fonts-cjk-sans-2.004/share/fonts/opentype/noto-cjk/NotoSansCJK-VF.otf.ttc"
SC_INDEX = 2  # JP, KR, SC, TC, HK ordering in Noto CJK ttc

OUT = Path(__file__).resolve().parent.parent / "assets"

# palette: 2bpp code -> RGB, MUST be ordered by code (argmin index == code)
PALETTE = [
    (0, 0, 0),        # 0 black
    (255, 255, 255),  # 1 white
    (250, 195, 25),   # 2 yellow
    (205, 35, 50),    # 3 red
]

DIGIT_BOX_W, DIGIT_BOX_H = 28, 56
DIGIT_ADVANCE = 29
DIGIT_Y = 66          # top of the digit row
DIGIT_RIGHT = 145     # right edge of the last digit


def fpx(v):
    return v * S


def load_font(size):
    f = ImageFont.truetype(FONT, size, index=SC_INDEX)
    try:
        f.set_variation_by_axes([700])  # bold strokes survive e-paper quantization
    except Exception:
        pass
    return f


def draw_canvas():
    """Draw the full static background at supersampled scale, return 200x200 RGB."""
    img = Image.new("RGB", (W * S, H * S), (255, 255, 255))
    d = ImageDraw.Draw(img)

    black = (0, 0, 0)
    red = (205, 35, 50)
    yellow = (250, 195, 25)
    white = (255, 255, 255)

    # ---- header: 我们在一起 ----
    f_header = load_font(20 * S)
    d.text((fpx(100), fpx(22)), "我们在一起", font=f_header, fill=black, anchor="mm")

    # ---- red heart, top-left (center 18,20) ----
    heart(d, fpx(18), fpx(21), fpx(9), red, black)

    # ---- 第 ... 天 frame around the digit area ----
    f_frame = load_font(26 * S)
    d.text((fpx(17), fpx(94)), "第", font=f_frame, fill=black, anchor="mm")
    d.text((fpx(164), fpx(94)), "天", font=f_frame, fill=black, anchor="mm")

    # ---- jasmine flower, bottom-right (center 160,150) ----
    jasmine(d, fpx(160), fpx(151), fpx(26), white, black, yellow)

    # ---- small red heart between the two bottom dates (center 92,186) ----
    heart(d, fpx(119), fpx(186), fpx(7), red, black)

    img = img.resize((W, H), Image.LANCZOS)
    return img


def heart(d, cx, cy, r, fill, outline):
    """Heart: two circles + a triangle, outline drawn slightly larger."""
    d.polygon(
        [(cx - r, cy - r * 0.2), (cx + r, cy - r * 0.2), (cx, cy + r * 1.5)],
        fill=outline,
    )
    d.ellipse(
        [cx - r * 1.15, cy - r * 1.3, cx - r * 0.05, cy - r * 0.15],
        fill=outline,
    )
    d.ellipse(
        [cx + r * 0.05, cy - r * 1.3, cx + r * 1.15, cy - r * 0.15],
        fill=outline,
    )
    r2 = r * 0.82
    d.polygon(
        [(cx - r2, cy - r2 * 0.2), (cx + r2, cy - r2 * 0.2), (cx, cy + r2 * 1.5)],
        fill=fill,
    )
    d.ellipse([cx - r2 * 1.15, cy - r2 * 1.3, cx - r2 * 0.05, cy - r2 * 0.15], fill=fill)
    d.ellipse([cx + r2 * 0.05, cy - r2 * 1.3, cx + r2 * 1.15, cy - r2 * 0.15], fill=fill)


def jasmine(d, cx, cy, r, petal_fill, petal_outline, center_fill):
    """Six-petal jasmine: rotated ellipse petals, yellow center."""
    import math

    n = 6
    for i in range(n):
        ang = math.radians(i * 360 / n - 90)
        px, py = cx + r * 0.62 * math.cos(ang), cy + r * 0.62 * math.sin(ang)
        a, b = r * 0.62, r * 0.30  # petal semi-axes
        pts = []
        for t in range(36):
            th = math.radians(t * 10)
            # ellipse in petal-local frame, rotated by ang
            x = a * math.cos(th)
            y = b * math.sin(th)
            pts.append(
                (
                    px + x * math.cos(ang) - y * math.sin(ang),
                    py + x * math.sin(ang) + y * math.cos(ang),
                )
            )
        d.polygon(pts, fill=petal_outline)  # black silhouette (slightly bigger)
        pts2 = []
        for t in range(36):
            th = math.radians(t * 10)
            x = a * 0.86 * math.cos(th)
            y = b * 0.72 * math.sin(th)
            pts2.append(
                (
                    px + x * math.cos(ang) - y * math.sin(ang),
                    py + x * math.sin(ang) + y * math.cos(ang),
                )
            )
        d.polygon(pts2, fill=petal_fill)
    # yellow center + black ring + stamen dots
    d.ellipse([cx - r * 0.24, cy - r * 0.24, cx + r * 0.24, cy + r * 0.24], fill=petal_outline)
    d.ellipse(
        [cx - r * 0.19, cy - r * 0.19, cx + r * 0.19, cy + r * 0.19], fill=center_fill
    )
    for i in range(n):
        ang = math.radians(i * 360 / n - 90)
        dx, dy = r * 0.115 * math.cos(ang), r * 0.115 * math.sin(ang)
        d.ellipse(
            [cx + dx - fpx(0.7), cy + dy - fpx(0.7), cx + dx + fpx(0.7), cy + dy + fpx(0.7)],
            fill=petal_outline,
        )


def battery_icon(d, x, y, w, h):
    """Small battery outline with a low red bar, top-left anchor (200-scale px).
    Drawn on a supersampled canvas: all coordinates go through fpx(), stroke
    widths are given in FINAL pixels and scaled by S here."""
    black = (0, 0, 0)
    red = (205, 35, 50)
    t = max(1.2, w * 0.08) * S  # outline thickness, final px -> supersampled
    # body
    d.rounded_rectangle(
        [fpx(x), fpx(y), fpx(x + w), fpx(y + h)],
        radius=fpx(2), outline=black, width=int(t),
    )
    # terminal nub on the right
    nw = w * 0.12
    d.rounded_rectangle(
        [fpx(x + w), fpx(y + h * 0.3), fpx(x + w + nw), fpx(y + h * 0.7)],
        radius=fpx(1), fill=black,
    )
    # low red bar
    d.rounded_rectangle(
        [fpx(x + w * 0.12), fpx(y + h * 0.25), fpx(x + w * 0.38), fpx(y + h * 0.75)],
        radius=fpx(1), fill=red,
    )


def draw_charge_screen():
    """Full-screen 'please charge' warning on an inverted black background."""
    img = Image.new("RGB", (W * S, H * S), (0, 0, 0))
    d = ImageDraw.Draw(img)

    black = (0, 0, 0)
    red = (205, 35, 50)
    yellow = (250, 195, 25)
    white = (255, 255, 255)

    # thin white frame so the inverted screen looks intentional
    d.rectangle([fpx(2), fpx(2), fpx(W - 3), fpx(H - 3)], outline=white, width=2 * S // 2)

    # ---- big empty battery, center-top (white outline on black) ----
    bx, by, bw, bh = 58, 34, 84, 46
    d.rounded_rectangle(
        [fpx(bx), fpx(by), fpx(bx + bw), fpx(by + bh)],
        radius=fpx(4), outline=white, width=3 * S // 2,
    )
    d.rounded_rectangle(
        [fpx(bx + bw), fpx(by + bh * 0.28), fpx(bx + bw + 9), fpx(by + bh * 0.72)],
        radius=fpx(2), fill=white,
    )
    # red exclamation inside
    d.rounded_rectangle(
        [fpx(bx + bw / 2 - 5), fpx(by + 7), fpx(bx + bw / 2 + 5), fpx(by + bh - 14)],
        radius=fpx(2), fill=red,
    )
    d.ellipse(
        [fpx(bx + bw / 2 - 5), fpx(by + bh - 12), fpx(bx + bw / 2 + 5), fpx(by + bh - 2)],
        fill=red,
    )

    # ---- 请充电 (white: most readable on black) ----
    f_big = load_font(30 * S)
    d.text((fpx(100), fpx(112)), "请充电", font=f_big, fill=white, anchor="mm")

    # ---- small reassurance line (yellow accent pops on black) ----
    f_small = load_font(13 * S)
    d.text((fpx(100), fpx(140)), "接上 USB 稍作休息", font=f_small, fill=yellow, anchor="mm")

    # ---- motif: heart + jasmine (colors adjusted for black bg) ----
    heart(d, fpx(38), fpx(168), fpx(8), red, red)
    jasmine(d, fpx(162), fpx(167), fpx(15), black, white, yellow)

    img = img.resize((W, H), Image.LANCZOS)
    return img


def quantize(img):
    """200x200 RGB -> index array (0..3).

    Anti-aliased edges land between palette colors; a nearest-RGB match sends
    gray strokes to red (gray is closer to red than to black). So: low-saturation
    pixels go to black/white by luminance, only saturated pixels pick red/yellow.
    """
    a = np.asarray(img).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    sat = a.max(-1) - a.min(-1)
    lum = (r * 299 + g * 587 + b * 114) // 1000
    grayish = sat < 90
    black_sel = grayish & (lum < 140)
    white_sel = grayish & ~black_sel
    d_red = (r - 205) ** 2 + (g - 35) ** 2 + (b - 50) ** 2
    d_yel = (r - 250) ** 2 + (g - 195) ** 2 + (b - 25) ** 2
    red_sel = ~grayish & (d_red <= d_yel)
    yel_sel = ~grayish & (d_red > d_yel)
    idx = np.zeros(a.shape[:2], np.uint8)
    idx[black_sel] = 0
    idx[white_sel] = 1
    idx[yel_sel] = 2
    idx[red_sel] = 3
    return idx


def pack_2bpp(idx):
    b = idx.reshape(H, W // 4, 4)
    packed = (b[..., 0] << 6) | (b[..., 1] << 4) | (b[..., 2] << 2) | b[..., 3]
    return packed.astype(np.uint8).tobytes()


def digit_masks():
    """10 fixed-box 1bpp masks (28x56), rendered from the CJK font's digits."""
    row_bytes = (DIGIT_BOX_W + 7) // 8
    masks = []
    for ch in "0123456789":
        # render at 4x the box for clean downscale
        img = Image.new("L", (DIGIT_BOX_W * S, DIGIT_BOX_H * S), 0)
        d = ImageDraw.Draw(img)
        f = load_font(int(DIGIT_BOX_H * S * 0.78))
        d.text(
            (DIGIT_BOX_W * S // 2, DIGIT_BOX_H * S // 2),
            ch,
            font=f,
            fill=255,
            anchor="mm",
        )
        img = img.resize((DIGIT_BOX_W, DIGIT_BOX_H), Image.LANCZOS)
        a = np.asarray(img)
        bits = (a >= 128).astype(np.uint8)
        m = bytearray(row_bytes * DIGIT_BOX_H)
        for yy in range(DIGIT_BOX_H):
            for xx in range(DIGIT_BOX_W):
                if bits[yy, xx]:
                    m[yy * row_bytes + xx // 8] |= 0x80 >> (xx % 8)
        masks.append(bytes(m))
    return masks


def compose_preview(idx_bg, day=835, dates=True):
    """Composite background + red digits + runtime-style date strings -> PNG."""
    rgb = np.zeros((H, W, 3), dtype=np.uint8)
    for code, (r, g, b) in enumerate(PALETTE):
        rgb[idx_bg == code] = (r, g, b)
    img = Image.fromarray(rgb)

    # digits (draw masks in red), right-aligned
    if day is not None:
        s = str(day)
        x = DIGIT_RIGHT - DIGIT_BOX_W
        for ch in reversed(s):
            m = digit_masks()[int(ch)]
            row_bytes = (DIGIT_BOX_W + 7) // 8
            for yy in range(DIGIT_BOX_H):
                for xx in range(DIGIT_BOX_W):
                    if m[yy * row_bytes + xx // 8] & (0x80 >> (xx % 8)):
                        img.putpixel((x + xx, DIGIT_Y + yy), red)
            x -= DIGIT_ADVANCE

    # runtime-style bottom row (FONT_6X12 in firmware): 6x12 ascii
    if dates:
        f = load_font(11)
        d = ImageDraw.Draw(img)
        d.text((8, 186), "S.2024.06.01", font=f, fill=black, anchor="lm")
        d.text((136, 186), "2026.09.14", font=f, fill=black, anchor="lm")

    return img.resize((W * 3, H * 3), Image.NEAREST)


black = (0, 0, 0)
red = (205, 35, 50)


def main():
    OUT.mkdir(exist_ok=True)

    # ---- normal day screen ----
    idx = quantize(draw_canvas())
    (OUT / "background.2bpp").write_bytes(pack_2bpp(idx))
    assert idx.max() <= 3

    # ---- low-battery screen: same layout + corner warning icon ----
    img_low = draw_canvas_with_icon()
    idx_low = quantize(img_low)
    (OUT / "background_low.2bpp").write_bytes(pack_2bpp(idx_low))

    # ---- charge warning screen ----
    idx_chg = quantize(draw_charge_screen())
    (OUT / "screen_charge.2bpp").write_bytes(pack_2bpp(idx_chg))

    masks = digit_masks()
    (OUT / "digits.1bpp").write_bytes(b"".join(masks))

    compose_preview(idx, day=835).save(OUT / "preview_normal.png")
    compose_preview(idx_low, day=836).save(OUT / "preview_low.png")
    compose_preview(idx_chg, day=None, dates=False).save(OUT / "preview_charge.png")

    print("assets written:", *(p.name for p in sorted(OUT.iterdir())))


def draw_canvas_with_icon():
    """Normal canvas plus the low-battery icon, drawn pre-downscale for clean strokes."""
    img = Image.new("RGB", (W * S, H * S), (255, 255, 255))
    d = ImageDraw.Draw(img)
    # replicate normal canvas draws, then add icon at supersample coords
    base = draw_canvas_raw(d)
    battery_icon(d, 168, 14, 22, 12)
    return img.resize((W, H), Image.LANCZOS)


def draw_canvas_raw(d):
    """Draw normal-canvas content onto an existing supersampled image."""
    black = (0, 0, 0)
    red = (205, 35, 50)

    f_header = load_font(20 * S)
    d.text((fpx(100), fpx(22)), "我们在一起", font=f_header, fill=black, anchor="mm")
    heart(d, fpx(18), fpx(21), fpx(9), red, black)
    f_frame = load_font(26 * S)
    d.text((fpx(17), fpx(94)), "第", font=f_frame, fill=black, anchor="mm")
    d.text((fpx(164), fpx(94)), "天", font=f_frame, fill=black, anchor="mm")
    jasmine(d, fpx(160), fpx(151), fpx(26), (255, 255, 255), black, (250, 195, 25))
    heart(d, fpx(119), fpx(186), fpx(7), red, black)


if __name__ == "__main__":
    main()
