#include "test_harness.h"

int g_check_fails = 0;

int main()
{
    run_civil_tests();
    run_battery_tests();
    run_store_tests();
    run_framebuffer_tests();
    run_screen_tests();
    run_version_tests();
    if (g_check_fails != 0) {
        fprintf(stderr, "%d check(s) FAILED\n", g_check_fails);
        return 1;
    }
    printf("all host tests passed\n");
    return 0;
}
