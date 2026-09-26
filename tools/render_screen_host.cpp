// Host-side render of what the firmware will compose: same screen_draw()
// path as the device, output as a PPM for visual inspection.
//   g++ -std=c++17 -Ifirmware/src tools/render_screen_host.cpp \
//       firmware/src/{civil,battery,framebuffer,assets,fonts,screen}.cpp \
//       -o /tmp/render_host && /tmp/render_host
#include <stdio.h>

#include "battery.h"
#include "civil.h"
#include "framebuffer.h"
#include "screen.h"

static Date D(int y, int m, int d)
{
    Date dt;
    dt.y = y;
    dt.m = (uint8_t)m;
    dt.d = (uint8_t)d;
    return dt;
}

// 2bpp code -> RGB (same palette as prerender.py)
static const int PAL[4][3] = {{0, 0, 0}, {255, 255, 255}, {250, 195, 25}, {205, 35, 50}};

static void write_ppm(const char *path, const uint8_t *fb)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n200 200\n255\n");
    for (int i = 0; i < FB_FRAME_LEN; i++) {
        for (int px = 6; px >= 0; px -= 2) {
            int code = (fb[i] >> px) & 0b11;
            fwrite(PAL[code], 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    static uint8_t fb[FB_FRAME_LEN];
    // same demo values as prerender.py compose(): 122 days
    Date anniv = D(2026, 5, 16);
    Date today = D(2026, 9, 14);
    screen_draw(fb, BATT_NORMAL, anniv, 122, today, true); // wifi ok
    write_ppm("/tmp/firmware_preview_normal.ppm", fb);

    static uint8_t fb2[FB_FRAME_LEN];
    screen_draw(fb2, BATT_CRITICAL, anniv, 122, today, false);
    write_ppm("/tmp/firmware_preview_charge.ppm", fb2);

    static uint8_t fb3[FB_FRAME_LEN];
    screen_draw(fb3, BATT_LOW, anniv, 122, today, false); // wifi fail
    write_ppm("/tmp/firmware_preview_low.ppm", fb3);

    static uint8_t fb4[FB_FRAME_LEN];
    screen_draw_setup(fb4);
    write_ppm("/tmp/firmware_preview_setup.ppm", fb4);
    printf("written /tmp/firmware_preview_{normal,charge,low,setup}.ppm\n");
    return 0;
}
