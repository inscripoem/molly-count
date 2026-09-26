// screen composition tests: background selection, digit placement, text
// rows, and the fully-static charge screen.
#include <string.h>

#include "assets.h"
#include "battery.h"
#include "civil.h"
#include "framebuffer.h"
#include "screen.h"
#include "test_harness.h"

static uint8_t fb[FB_FRAME_LEN];

static Date D(int y, int m, int d)
{
    Date dt;
    dt.y = y;
    dt.m = (uint8_t)m;
    dt.d = (uint8_t)d;
    return dt;
}

static int row_diffs(const uint8_t *a, const uint8_t *b, int y)
{
    return memcmp(a + y * FB_ROW_BYTES, b + y * FB_ROW_BYTES, FB_ROW_BYTES) != 0;
}

void run_screen_tests()
{
    Date anniv = D(2026, 5, 16);
    Date today = D(2026, 9, 14);

    // charge warning is fully static: byte-identical to the asset
    screen_draw(fb, BATT_CRITICAL, anniv, 122, today, false);
    CHECK(memcmp(fb, SCREEN_CHARGE, FB_FRAME_LEN) == 0);

    // normal screen: background + digits + both text rows. Rows outside the
    // three runtime regions must stay byte-identical to the background.
    screen_draw(fb, BATT_NORMAL, anniv, 122, today, false);
    for (int y = 0; y < FB_HEIGHT; y++) {
        bool runtime_row = (y >= 9 && y < 21) ||        // date (top 18-9, 12 rows)
                           (y >= DIGIT_Y && y < DIGIT_Y + DIGIT_BOX_H) || // digits
                           (y >= 164 && y < 179);         // SINCE (top 171-7, 15 rows)
        if (runtime_row) continue;
        CHECK(!row_diffs(fb, BACKGROUND, y));
        if (g_check_fails) return;
    }

    // day digits actually drawn over the white hole
    CHECK(memcmp(fb + DIGIT_Y * FB_ROW_BYTES,
                 BACKGROUND + DIGIT_Y * FB_ROW_BYTES,
                 DIGIT_BOX_H * FB_ROW_BYTES) != 0);

    // single-digit day count touches only the rightmost digit box
    screen_draw(fb, BATT_NORMAL, anniv, 5, today, false);
    for (int y = DIGIT_Y; y < DIGIT_Y + DIGIT_BOX_H; y++) {
        for (int x = 0; x < DIGIT_RIGHT - DIGIT_BOX_W; x++) {
            int idx = y * FB_ROW_BYTES + x / 4;
            int shift = 6 - (x % 4) * 2;
            uint8_t fb_px = (fb[idx] >> shift) & 0b11;
            uint8_t bg_px = (BACKGROUND[idx] >> shift) & 0b11;
            CHECK(fb_px == bg_px);
            if (g_check_fails) return;
        }
    }

    // low screen picks BACKGROUND_LOW
    screen_draw(fb, BATT_LOW, anniv, 122, today, false);
    CHECK(memcmp(fb + 180 * FB_ROW_BYTES, BACKGROUND_LOW + 180 * FB_ROW_BYTES,
                 10 * FB_ROW_BYTES) == 0);

    // network status mark, top-right (6x12, rows 9..20): "WiFi" at x170
    // when the wake's network attempt succeeded, "NO WIFI" at x152 when it
    // failed. Distinguish them by the x152..168 strip only "NO WIFI" reaches.
    auto ink = [&](int x0, int x1) {
        int n = 0;
        for (int y = 9; y < 21; y++) {
            for (int x = x0; x < x1; x++) {
                int idx = y * FB_ROW_BYTES + x / 4;
                int shift = 6 - (x % 4) * 2;
                if (((fb[idx] >> shift) & 0b11) == FB_BLACK) n++;
            }
        }
        return n;
    };

    screen_draw(fb, BATT_NORMAL, anniv, 122, today, false); // wifi failed
    CHECK(ink(152, 169) > 20);  // "NO WIFI" glyph ink
    screen_draw(fb, BATT_NORMAL, anniv, 122, today, true);  // wifi ok
    CHECK(ink(170, 194) > 15);  // "WiFi" glyph ink
    CHECK(ink(152, 169) == 0);  // nothing left of it

    // setup screen is the static generated asset
    screen_draw_setup(fb);
    CHECK(memcmp(fb, SCREEN_SETUP, FB_FRAME_LEN) == 0);
}
