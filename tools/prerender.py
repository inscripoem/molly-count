#!/usr/bin/env python3
"""Generates molly-count display assets.

Design: Swiss grid + Arknights Lone Trail label elements.

Layout (200x200), top to bottom:
  line 1  runtime date (left) / battery icon (right, low state only)
  line 2  tracked title 我们在一起 (binary-rendered)
  hero    runtime day digits (80px masks) + red 天 badge
  rules   red / yellow / ink triple line
  row     chip + runtime SINCE_ text (single row)
  footer  black bar with label codes

All saturated (red/yellow) and on-black texts are binary-rendered
(masked, no anti-aliasing) so quantization never speckles them.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W = H = 200
S = 4  # supersample factor
FONT = "/nix/store/dllg6pqphyxipzwihzb6phmjlq4r0w4b-noto-fonts-cjk-sans-2.004/share/fonts/opentype/noto-cjk/NotoSansCJK-VF.otf.ttc"
SC_INDEX = 2

OUT = Path(__file__).resolve().parent.parent / "assets"

BLACK, WHITE = (0, 0, 0), (255, 255, 255)
RED, YELLOW = (205, 35, 50), (250, 195, 25)

# 2bpp code -> RGB, ordered by code (quantizer index == code)
PALETTE = [
    BLACK,
    WHITE,
    YELLOW,
    RED,
]

# digit glyph geometry — keep in sync with src/assets.rs
DIGIT_BOX_W, DIGIT_BOX_H = 52, 88
DIGIT_ROW_BYTES = (DIGIT_BOX_W + 7) // 8
DIGIT_ADVANCE = 52
DIGIT_Y = 46           # mask top; glyph baseline sits at DIGIT_Y+76
DIGIT_BASELINE_IN_BOX = 76
DIGIT_RIGHT = 164      # right edge of the last digit box


def fpx(*vs):
    return [v * S for v in vs]


def load_font(size, weight=700, raw=False):
    px = int(size if raw else size * S)
    f = ImageFont.truetype(FONT, px, index=SC_INDEX)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def masked_text(img, x, y, text, size, weight, color, anchor="lm"):
    """Binary text (no anti-aliasing): crisp edges, no quantizer fringing."""
    mask = Image.new("L", (W * S, H * S), 0)
    md = ImageDraw.Draw(mask)
    md.text((x * S, y * S), text, font=load_font(size, weight), fill=255, anchor=anchor)
    mask = mask.point(lambda v: 255 if v >= 128 else 0)
    img.paste(Image.new("RGB", img.size, color), (0, 0), mask)


def masked_tracked(img, x, y, s, size, weight, color, step):
    mask = Image.new("L", (W * S, H * S), 0)
    md = ImageDraw.Draw(mask)
    f = load_font(size, weight)
    for i, ch in enumerate(s):
        md.text(((x + i * step) * S, y * S), ch, font=f, fill=255, anchor="lm")
    mask = mask.point(lambda v: 255 if v >= 128 else 0)
    img.paste(Image.new("RGB", img.size, color), (0, 0), mask)


def battery(d, x, y, w, h):
    t = int(1.2 * S)
    d.rounded_rectangle(fpx(x, y, x + w, y + h), radius=1 * S, outline=BLACK, width=t)
    d.rounded_rectangle(fpx(x + w, y + h * 0.3, x + w + w * 0.12, y + h * 0.7),
                        radius=0.5 * S, fill=BLACK)
    d.rounded_rectangle(fpx(x + w * 0.12, y + h * 0.25, x + w * 0.38, y + h * 0.75),
                        radius=0.5 * S, fill=RED)


def chip(d, x, y, color):
    d.rectangle(fpx(x, y - 8, x + 6, y + 8), fill=color)


def rules(d, ink):
    d.rectangle(fpx(8, 136, 192, 140), fill=RED)    # 4px
    d.rectangle(fpx(8, 142, 192, 145), fill=YELLOW) # 3px
    d.rectangle(fpx(8, 147, 192, 150), fill=ink)    # 3px, white on charge


def footer(img, d, ink, day_code=False):
    d.rectangle(fpx(0, 184, W, 200), fill=ink)
    bar_ink = WHITE if ink == BLACK else BLACK
    masked_text(img, 8, 192, "MC 2026 // STEREO", 9, 700, bar_ink)
    masked_text(img, 192, 192, "E", 9, 900, YELLOW, anchor="rm")


def draw_main(mode):
    """mode: normal | low | charge (charge on an inverted black field)"""
    charge = mode == "charge"
    ink = WHITE if charge else BLACK
    img = Image.new("RGB", (W * S, H * S), BLACK if charge else WHITE)
    d = ImageDraw.Draw(img)

    # line 1: battery icon top-right on the low screen; date is runtime
    if mode == "low":
        battery(d, 166, 6, 28, 15)

    # line 2: tracked title (binary-rendered for clean strokes)
    masked_tracked(img, 10, 36, "我们在一起", 16, 900, ink, 21)


    if mode == "charge":
        masked_text(img, 100, 80, "请充电", 36, 900, ink, anchor="mm")
        masked_text(img, 100, 112, "接上 USB 稍作休息", 13, 500, YELLOW, anchor="mm")
    else:
        # hero area stays white: digits are runtime-blitted from masks
        d.rounded_rectangle(fpx(158, 98, 184, 122), radius=1 * S, fill=RED)
        masked_text(img, 171, 110, "天", 16, 700, WHITE, anchor="mm")

    rules(d, ink)

    if mode == "charge":
        chip(d, 8, 171, YELLOW)
        masked_text(img, 21, 171, "LOW POWER", 10, 600, ink)
        masked_text(img, 192, 171, "USB REQUIRED", 9, 500, ink, anchor="rm")
    else:
        chip(d, 8, 171, BLACK)
        # SINCE_ text is runtime (firmware), row stays empty

    footer(img, d, ink)

    return img.resize((W, H), Image.LANCZOS)


def quantize(img):
    """200x200 RGB -> index array (0..3). Low-saturation pixels go to
    black/white by luminance; saturated pixels pick red/yellow."""
    a = np.asarray(img).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    sat = a.max(-1) - a.min(-1)
    lum = (r * 299 + g * 587 + b * 114) // 1000
    grayish = sat < 90
    black_sel = grayish & (lum < 140)
    d_red = (r - 205) ** 2 + (g - 35) ** 2 + (b - 50) ** 2
    d_yel = (r - 250) ** 2 + (g - 195) ** 2 + (b - 25) ** 2
    idx = np.zeros(a.shape[:2], np.uint8)
    idx[grayish & ~black_sel] = 1
    idx[~grayish & (d_red > d_yel)] = 2
    idx[~grayish & (d_red <= d_yel)] = 3
    return idx


def pack_2bpp(idx):
    b = idx.reshape(H, W // 4, 4)
    packed = (b[..., 0] << 6) | (b[..., 1] << 4) | (b[..., 2] << 2) | b[..., 3]
    return packed.astype(np.uint8).tobytes()


def digit_masks():
    """Each glyph rendered on its BASELINE (anchor ms) so CJK font metrics
    cannot push ink to the box bottom (which would fuse with the rules)."""
    row_bytes = (DIGIT_BOX_W + 7) // 8
    masks = []
    for chn in "0123456789":
        img = Image.new("L", (DIGIT_BOX_W * S, DIGIT_BOX_H * S), 0)
        d = ImageDraw.Draw(img)
        d.text(
            (DIGIT_BOX_W * S // 2, DIGIT_BASELINE_IN_BOX * S),
            chn,
            font=load_font(80, 900),
            fill=255,
            anchor="ms",
        )
        img = img.resize((DIGIT_BOX_W, DIGIT_BOX_H), Image.LANCZOS)
        a = np.asarray(img)
        bits = (a >= 128).astype(np.uint8)
        m = bytearray(row_bytes * DIGIT_BOX_H)
        rows = [yy for yy in range(DIGIT_BOX_H) for xx in range(DIGIT_BOX_W)
                if bits[yy, xx]]
        assert rows, "empty digit mask"
        assert min(rows) >= 4 and max(rows) <= DIGIT_BOX_H - 6, \
            f"digit {chn} ink out of safe area: {min(rows)}..{max(rows)}"
        for yy in range(DIGIT_BOX_H):
            for xx in range(DIGIT_BOX_W):
                if bits[yy, xx]:
                    m[yy * row_bytes + xx // 8] |= 0x80 >> (xx % 8)
        masks.append(bytes(m))
    return masks


def compose(idx_bg, day, runtime_texts=True):
    rgb = np.zeros((H, W, 3), dtype=np.uint8)
    for code, (r, g, b) in enumerate(PALETTE):
        rgb[idx_bg == code] = (r, g, b)
    img = Image.fromarray(rgb)

    if day is not None:
        s = str(day)
        x_end = DIGIT_RIGHT
        masks = digit_masks()
        for ch in reversed(s):
            m = masks[int(ch)]
            for yy in range(DIGIT_BOX_H):
                for xx in range(DIGIT_BOX_W):
                    if m[yy * DIGIT_ROW_BYTES + xx // 8] & (0x80 >> (xx % 8)):
                        img.putpixel((x_end - DIGIT_BOX_W + xx, DIGIT_Y + yy), BLACK)
            x_end -= DIGIT_ADVANCE

    if runtime_texts:
        def rfont(size, weight=700):
            f = ImageFont.truetype(FONT, int(size), index=SC_INDEX)
            try:
                f.set_variation_by_axes([weight])
            except Exception:
                pass
            return f
        # approximates the firmware's FONT_9X15_BOLD runtime rendering
        d = ImageDraw.Draw(img)
        d.text((10, 12), "2026.09.14", font=rfont(8, 500), fill=BLACK, anchor="lm")
        d.text((21, 170), "SINCE 2024.01.01", font=rfont(12, 800), fill=BLACK, anchor="lm")

    return img.resize((W * 3, H * 3), Image.NEAREST)


def main():
    OUT.mkdir(exist_ok=True)

    idx = quantize(draw_main("normal"))
    (OUT / "background.2bpp").write_bytes(pack_2bpp(idx))
    assert idx.max() <= 3

    idx_low = quantize(draw_main("low"))
    (OUT / "background_low.2bpp").write_bytes(pack_2bpp(idx_low))

    idx_chg = quantize(draw_main("charge"))
    (OUT / "screen_charge.2bpp").write_bytes(pack_2bpp(idx_chg))

    (OUT / "digits.1bpp").write_bytes(b"".join(digit_masks()))

    compose(idx, 122).save(OUT / "preview_normal.png")
    compose(idx_low, 122).save(OUT / "preview_low.png")
    compose(idx_chg, None, runtime_texts=False).save(OUT / "preview_charge.png")

    print("assets written:", *(p.name for p in sorted(OUT.iterdir())))


if __name__ == "__main__":
    main()
