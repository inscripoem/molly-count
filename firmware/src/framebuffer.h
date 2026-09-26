// molly-count — 200x200 2bpp packed framebuffer for the 1.54" (G) four-color
// panel. Ported from src/framebuffer.rs; layout: 4 pixels per byte, 2 bits
// per pixel, MSB first, 50 bytes per row. Color codes: 0=black, 1=white,
// 2=yellow, 3=red (EPD_1IN54G_* defines). Pure logic, host-testable.
#pragma once

#include <stdint.h>

#define FB_WIDTH 200
#define FB_HEIGHT 200
#define FB_ROW_BYTES (FB_WIDTH / 4)
#define FB_FRAME_LEN (FB_ROW_BYTES * FB_HEIGHT)

// Color codes (identical to the vendor EPD_1IN54G_* defines).
#define FB_BLACK 0x0
#define FB_WHITE 0x1
#define FB_YELLOW 0x2
#define FB_RED 0x3

void fb_clear(uint8_t *fb, uint8_t color);

// Whole-buffer replace with a pre-rendered packed image (FB_FRAME_LEN bytes).
void fb_load(uint8_t *fb, const uint8_t *packed_img);

void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t color);

// Draw a 1bpp mask (MSB-first rows) in `color` at (x, y); clips at edges.
void fb_blit_mask(uint8_t *fb, int x, int y, int w, int h, int row_bytes,
                  const uint8_t *mask, uint8_t color);
