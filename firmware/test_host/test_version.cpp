// Ported-invariant tests for the firmware version comparator.
#include "test_harness.h"
#include "version.h"

void run_version_tests()
{
    CHECK(fw_ver_cmp("1.0", "1.0") == 0);
    CHECK(fw_ver_cmp("1.0", "1.1") < 0);
    CHECK(fw_ver_cmp("1.1", "1.0") > 0);
    CHECK(fw_ver_cmp("1.9", "1.10") < 0);   // numeric, not lexicographic
    CHECK(fw_ver_cmp("2.0", "1.99") > 0);
    CHECK(fw_ver_cmp("1", "1.0") == 0);     // missing segments = 0
    CHECK(fw_ver_cmp("1.2.3", "1.2") > 0);
    CHECK(fw_ver_cmp("", "0") == 0);
}
