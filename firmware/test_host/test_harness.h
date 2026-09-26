// Shared host-test harness.
#pragma once

#include <stdio.h>

extern int g_check_fails;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #cond);                                            \
            g_check_fails++;                                           \
        }                                                              \
    } while (0)

void run_civil_tests();
void run_battery_tests();
void run_store_tests();
void run_framebuffer_tests();
void run_screen_tests();
void run_version_tests();
