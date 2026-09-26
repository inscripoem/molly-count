// Ported from src/framebuffer.rs — packing semantics identical.
#include "framebuffer.h"

#include <string.h>

void fb_clear(uint8_t *fb, uint8_t color)
{
    uint8_t v = (uint8_t)((color << 6) | (color << 4) | (color << 2) | color);
    memset(fb, v, FB_FRAME_LEN);
}

void fb_load(uint8_t *fb, const uint8_t *packed_img)
{
    memcpy(fb, packed_img, FB_FRAME_LEN);
}

void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t color)
{
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
    int idx = y * FB_ROW_BYTES + x / 4;
    int shift = 6 - (x % 4) * 2;
    fb[idx] = (uint8_t)((fb[idx] & ~(0b11 << shift)) | (color << shift));
}

void fb_blit_mask(uint8_t *fb, int x, int y, int w, int h, int row_bytes,
                  const uint8_t *mask, uint8_t color)
{
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int px = x + xx;
            int py = y + yy;
            if (px < 0 || px >= FB_WIDTH || py < 0 || py >= FB_HEIGHT) continue;
            uint8_t bit = mask[yy * row_bytes + xx / 8] & (0x80 >> (xx % 8));
            if (bit != 0) {
                fb_set_pixel(fb, px, py, color);
            }
        }
    }
}
