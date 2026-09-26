// molly-count — screen composition: pre-rendered 2bpp background + runtime
// day digits + runtime date/SINCE text. Functional port of src/assets.rs +
// main.rs draw_screen(). Pure logic (text comes from generated fonts.h),
// host-testable.
#pragma once

#include <stdint.h>

#include "battery.h"
#include "civil.h"

// Screen kinds as persisted in the record (battery.h kind bytes):
// 0 normal, 1 low, 2 charge-warning.
const uint8_t *background_for(uint8_t batt_kind);

// Digits "0".."9" as 1bpp masks, DIGIT_BOX_W x DIGIT_BOX_H each.
const uint8_t *digit_mask(int d);

// Compose the final screen into fb (FB_FRAME_LEN bytes): static background
// for the battery state, black day digits, date (top-left) and SINCE row.
// The charge warning screen is fully static — no digits, no text.
// wifi_ok draws the top-right status mark: "WiFi" when the wake-cycle
// network attempt succeeded, "NO WIFI" when it failed.
void screen_draw(uint8_t *fb, BattState state, Date anniv, int64_t days,
                 Date today, bool wifi_ok);

// Full-screen provisioning instructions (generated SCREEN_SETUP asset).
void screen_draw_setup(uint8_t *fb);
