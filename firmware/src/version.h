// molly-count — firmware version helpers. Pure logic, host-testable.
#pragma once

// Current firmware version, set via platformio.ini build flag.
#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif

// Compare dotted numeric versions ("1.2" vs "1.10"). Returns <0 / 0 / >0.
int fw_ver_cmp(const char *a, const char *b);
