#!/usr/bin/env python3
"""Generates molly-count display assets.

Outputs (into ../assets/ relative to this file):
  background.2bpp      normal screen: everything static except the day digits
  background_low.2bpp  same + battery icon (top right)
  screen_charge.2bpp   full-screen static charge warning (inverted black)
  digits.1bpp          ten digit glyph masks, blitted at runtime in black
  preview_*.png        3x composites matching what the firmware renders

Design: Swiss-grid "Concept A" + Arknights Lone Trail label elements
(tracked header, 80px hero numeral, red 天 badge, red+yellow rules,
chip-labeled metadata row, black footer bar with codes).
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
    (0, 0, 0),        # 0 black
    (255, 255, 255),  # 1 white
    (250, 195, 25),   # 2 yellow
    (205, 35, 50),    # 3 red
]

# digit glyph geometry — keep in sync with src/assets.rs
DIGIT_BOX_W, DIGIT_BOX_H = 52, 88
DIGIT_ROW_BYTES = (DIGIT_BOX_W + 7) // 8
DIGIT_ADVANCE = 52
DIGIT_Y = 50           # top of digit row
DIGIT_RIGHT = 164      # right edge of the last digit box


def fpx(v):
    return v * S


def fp(*vs):
    return [v * S for v in vs]


def load_font(size, weight=700):
    f = ImageFont.truetype(FONT, int(size * S), index=SC_INDEX)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def tracked(d, x, y, s, size, weight, fill, step):
    f = load_font(size, weight)
    for i, ch in enumerate(s):
        d.text((x * S + i * step * S, y * S), ch, font=f, fill=fill, anchor="lm")


def battery(d, x, y, w, h):
    t = int(1.2 * S)
    d.rounded_rectangle(fp(x, y, x + w, y + h), radius=1 * S, outline=BLACK, width=t)
    d.rounded_rectangle(fp(x + w, y + h * 0.3, x + w + w * 0.12, y + h * 0.7),
                        radius=0.5 * S, fill=BLACK)
    d.rounded_rectangle(fp(x + w * 0.12, y + h * 0.25, x + w * 0.38, y + h * 0.75),
                        radius=0.5 * S, fill=RED)


def chip(d, x, y, color):
    d.rectangle(fp(x, y - 5.5, x + 3, y + 5.5), fill=color)


def header(d, ink):
    d.rectangle(fp(10, 11, 14, 15), fill=RED)
    tracked(d, 19, 13, "我们在一起", 9, 500, ink, 12)
    # top-right date is runtime text (firmware), area stays empty


def rules(d):
    d.rectangle(fp(8, 142, 192, 144), fill=RED)
    d.rectangle(fp(8, 146, 192, 147), fill=YELLOW)


def footer(d, ink, day_code=False):
    d.rectangle(fp(0, 184, W, 200), fill=ink)
    bar_ink = WHITE if ink == BLACK else BLACK
    d.text((8 * S, 192 * S), "MC_2026 // STEREO", font=load_font(8, 700),
           fill=bar_ink, anchor="lm")
    code = f"T-{day_code}" if day_code else "E"
    d.text((192 * S, 192 * S), code, font=load_font(8, 900), fill=YELLOW, anchor="rm")


def draw_main(mode):
    """mode: normal | low | charge"""
    ink = BLACK
    img = Image.new("RGB", (W * S, H * S), WHITE)
    d = ImageDraw.Draw(img)

    header(d, ink)

    if mode == "charge":
        d.text((100 * S, 82 * S), "请充电", font=load_font(34, 900), fill=ink, anchor="mm")
        d.text((100 * S, 114 * S), "接上 USB 稍作休息", font=load_font(11, 500),
               fill=YELLOW, anchor="mm")
    else:
        # hero area stays white: digits are runtime-blitted from masks
        d.rounded_rectangle(fp(152, 102, 178, 126), radius=1 * S, fill=RED)
        d.text((165 * S, 114 * S), "天", font=load_font(16, 700), fill=WHITE, anchor="mm")
        if mode == "low":
            battery(d, 168, 54, 26, 14)

    rules(d)

    if mode == "charge":
        chip(d, 8, 164, YELLOW)
        d.text((15 * S, 164 * S), "LOW_POWER", font=load_font(9, 600), fill=ink, anchor="lm")
        d.text((192 * S, 164 * S), "USB_REQUIRED", font=load_font(8, 500), fill=ink, anchor="rm")
    else:
        chip(d, 8, 164, BLACK)
        chip(d, 120, 164, RED)
        # SINCE_/TODAY_ texts are runtime (firmware), area stays empty

    if mode == "charge":
        footer(d, ink)
        d.text((8 * S, 172 * S), "PLUG_IN_USB // RESUME", font=load_font(7, 500),
               fill=ink, anchor="lm")
    else:
        footer(d, ink, day_code=122)

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
    row_bytes = (DIGIT_BOX_W + 7) // 8
    masks = []
    for chn in "0123456789":
        img = Image.new("L", (DIGIT_BOX_W * S, DIGIT_BOX_H * S), 0)
        d = ImageDraw.Draw(img)
        d.text(
            (DIGIT_BOX_W * S // 2, DIGIT_BOX_H * S // 2),
            chn,
            font=load_font(80, 900),
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


def compose(idx_bg, day, runtime_texts=True):
    rgb = np.zeros((H, W, 3), dtype=np.uint8)
    for code, (r, g, b) in enumerate(PALETTE):
        rgb[idx_bg == code] = (r, g, b)
    img = Image.fromarray(rgb)
    d = ImageDraw.Draw(img)

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
        f9 = load_font(9, 600)
        d.text((15 * S, 164 * S), "SINCE_2024.01.01", font=f9, fill=BLACK, anchor="lm")
        d.text((127 * S, 164 * S), "TODAY_09.14", font=load_font(9, 500), fill=BLACK, anchor="lm")
        d.text((192 * S, 17 * S), "2026.09.14", font=load_font(9, 500), fill=BLACK, anchor="rm")

    return img.resize((W * 3, H * 3), Image.NEAREST)


BLACK = (0, 0, 0)


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
