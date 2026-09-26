// Ported from src/framebuffer.rs #[cfg(test)] — same packing semantics.
#include <string.h>

#include "framebuffer.h"
#include "test_harness.h"

static uint8_t fb[FB_FRAME_LEN];

void run_framebuffer_tests()
{
    // new_is_all_white
    fb_clear(fb, FB_WHITE);
    int all55 = 1;
    for (int i = 0; i < FB_FRAME_LEN; i++) {
        if (fb[i] != 0x55) {
            all55 = 0;
            break;
        }
    }
    CHECK(all55);
    CHECK(FB_FRAME_LEN == 10000);

    // clear_packs_four_pixels_per_byte
    fb_clear(fb, FB_RED);
    CHECK(fb[0] == (uint8_t)0xFF && fb[9999] == (uint8_t)0xFF);
    fb_clear(fb, FB_YELLOW);
    CHECK(fb[0] == (uint8_t)0xAA);

    // set_pixel_is_msb_first
    fb_clear(fb, FB_BLACK);
    fb_set_pixel(fb, 0, 0, FB_RED);
    CHECK(fb[0] == 0b11000000);
    fb_set_pixel(fb, 3, 0, FB_YELLOW);
    CHECK(fb[0] == 0b11000010);
    fb_set_pixel(fb, 0, 1, FB_WHITE);
    CHECK(fb[50] == 0b01000000);

    // set_pixel_overwrites_without_touching_neighbors
    fb_clear(fb, FB_WHITE);
    fb_set_pixel(fb, 5, 7, FB_RED);
    int idx = 7 * FB_ROW_BYTES + 5 / 4;
    CHECK((fb[idx] & 0b00110000) == 0b00110000);
    CHECK((fb[idx] & 0b11001111) == 0b01000101);

    // out-of-range set_pixel is a no-op (Rust asserted; firmware clamps)
    fb_clear(fb, FB_WHITE);
    fb_set_pixel(fb, -1, 0, FB_RED);
    fb_set_pixel(fb, 200, 0, FB_RED);
    fb_set_pixel(fb, 0, 200, FB_RED);
    CHECK(fb[0] == 0x55 && fb[FB_FRAME_LEN - 1] == 0x55);

    // load_packed_replaces_content
    {
        static uint8_t bg[FB_FRAME_LEN];
        memset(bg, 0, sizeof bg);
        bg[123] = 0xAB;
        fb_load(fb, bg);
        CHECK(fb[123] == 0xAB);
        CHECK(fb[0] == 0);
    }

    // blit_mask_draws_only_set_bits
    fb_clear(fb, FB_WHITE);
    static const uint8_t mask1[1] = { 0b10110010 };
    fb_blit_mask(fb, 0, 0, 8, 1, 1, mask1, FB_RED);
    CHECK(fb[0] == (uint8_t)((3 << 6) | (1 << 4) | (3 << 2) | 3));

    // blit_mask_clips_at_edges
    fb_clear(fb, FB_WHITE);
    static const uint8_t mask2[1] = { 0xFF };
    fb_blit_mask(fb, 196, 0, 8, 1, 1, mask2, FB_RED);
    CHECK(fb[49] == (uint8_t)0xFF);
    CHECK(fb[50] == 0x55);
}
