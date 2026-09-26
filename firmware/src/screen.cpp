// Functional port of src/assets.rs draw_day_background + main.rs
// draw_screen(): static background per battery state, black day digits
// blitted from generated masks, then date (top-left, 6x12, alphabetic
// baseline at y=18) and SINCE row (9x15 bold, middle baseline at y=171).
#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "fonts.h"
#include "framebuffer.h"

const uint8_t *background_for(uint8_t batt_kind)
{
    switch (batt_kind) {
    case 1: return BACKGROUND_LOW;
    case 2: return SCREEN_CHARGE;
    default: return BACKGROUND;
    }
}

const uint8_t *digit_mask(int d)
{
    static const int len = DIGIT_ROW_BYTES * DIGIT_BOX_H;
    return &DIGITS[d * len];
}

// embedded-graphics Baseline semantics: glyph top edge = y - offset with
// Top = 0, Middle = (height - 1) / 2, Alphabetic = font baseline.
typedef enum { BASE_TOP, BASE_MIDDLE, BASE_ALPHABETIC } Baseline;

static void draw_text(uint8_t *fb, int x, int y, const char *s, int font,
                      Baseline baseline, uint8_t color)
{
    int char_w, char_h, row_bytes, base, glyph_len;
    const uint8_t *data;
    if (font == 0) {
        char_w = FONT6_W; char_h = FONT6_H; row_bytes = FONT6_ROW_BYTES;
        base = FONT6_BASELINE; data = font6x12;
        glyph_len = FONT6_H * FONT6_ROW_BYTES;
    } else {
        char_w = FONT9_W; char_h = FONT9_H; row_bytes = FONT9_ROW_BYTES;
        base = FONT9_BASELINE; data = font9x15_bold;
        glyph_len = FONT9_H * FONT9_ROW_BYTES;
    }
    int top;
    switch (baseline) {
    case BASE_TOP: top = y; break;
    case BASE_MIDDLE: top = y - (char_h - 1) / 2; break;
    default: top = y - base; break;
    }
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) { x += char_w; continue; }
        const uint8_t *glyph = &data[(c - 32) * glyph_len];
        fb_blit_mask(fb, x, top, char_w, char_h, row_bytes, glyph, color);
        x += char_w;
    }
}

void screen_draw_setup(uint8_t *fb)
{
    fb_load(fb, SCREEN_SETUP);
}

void screen_draw(uint8_t *fb, BattState state, Date anniv, int64_t days, Date today,
                 bool wifi_ok)
{
    // static art (per battery state) + black day digits
    fb_load(fb, background_for(batt_kind_byte(state)));

    // the charge warning screen is fully static: no digits, no date row
    if (state == BATT_CRITICAL) {
        return;
    }

    // top-left date, "YYYY.MM.DD" (chip slots live in the static background)
    char buf[24];
    snprintf(buf, sizeof buf, "%04d.%02d.%02d", today.y, today.m, today.d);
    draw_text(fb, 10, 18, buf, 0, BASE_ALPHABETIC, FB_BLACK);

    // top-right network status mark (6x12, same row as the date): "WiFi"
    // when this wake's network attempt succeeded, "NO WIFI" when it failed.
    // The low-battery screen has the battery icon at x166-194, so shift
    // the marker left of it.
    if (state == BATT_LOW) {
        draw_text(fb, wifi_ok ? 138 : 120, 18, wifi_ok ? "WiFi" : "NO WIFI", 0,
                  BASE_ALPHABETIC, FB_BLACK);
    } else {
        draw_text(fb, wifi_ok ? 170 : 152, 18, wifi_ok ? "WiFi" : "NO WIFI", 0,
                  BASE_ALPHABETIC, FB_BLACK);
    }

    // day digits, right-aligned into the white hole; negative counts render
    // their magnitude (same as the Rust firmware's days.abs())
    long long mag = (days < 0) ? -days : days;
    snprintf(buf, sizeof buf, "%lld", mag);
    int x_end = DIGIT_RIGHT;
    for (const char *p = buf + strlen(buf) - 1; p >= buf; p--) {
        int d = *p - '0';
        fb_blit_mask(fb, x_end - DIGIT_BOX_W, DIGIT_Y, DIGIT_BOX_W, DIGIT_BOX_H,
                     DIGIT_ROW_BYTES, digit_mask(d), FB_BLACK);
        x_end -= DIGIT_ADVANCE;
    }

    snprintf(buf, sizeof buf, "SINCE %04d.%02d.%02d", anniv.y, anniv.m, anniv.d);
    draw_text(fb, 21, 171, buf, 1, BASE_MIDDLE, FB_BLACK);
}
