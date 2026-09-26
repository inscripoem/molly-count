// Ported from src/battery.rs #[cfg(test)] — same cases.
#include "battery.h"
#include "test_harness.h"

void run_battery_tests()
{
    // normal_until_thresholds
    CHECK(battery_next_state(BATT_NORMAL, 4200) == BATT_NORMAL);
    CHECK(battery_next_state(BATT_NORMAL, 3600) == BATT_NORMAL);
    CHECK(battery_next_state(BATT_NORMAL, 3549) == BATT_LOW);
    CHECK(battery_next_state(BATT_NORMAL, 3349) == BATT_CRITICAL);

    // low_hysteresis
    CHECK(battery_next_state(BATT_LOW, 3550) == BATT_LOW);
    CHECK(battery_next_state(BATT_LOW, 3749) == BATT_LOW);
    CHECK(battery_next_state(BATT_LOW, 3750) == BATT_NORMAL);
    CHECK(battery_next_state(BATT_LOW, 3300) == BATT_CRITICAL);

    // critical_hysteresis
    CHECK(battery_next_state(BATT_CRITICAL, 3350) == BATT_CRITICAL);
    CHECK(battery_next_state(BATT_CRITICAL, 3449) == BATT_CRITICAL);
    CHECK(battery_next_state(BATT_CRITICAL, 3450) == BATT_LOW);
    CHECK(battery_next_state(BATT_CRITICAL, 3750) == BATT_NORMAL);

    // kind_byte_roundtrip
    const BattState all[] = { BATT_NORMAL, BATT_LOW, BATT_CRITICAL };
    for (BattState s : all) {
        CHECK(batt_from_kind_byte(batt_kind_byte(s)) == s);
    }

    // legacy_and_garbage_kind_bytes_decode_normal
    CHECK(batt_from_kind_byte(0xFF) == BATT_NORMAL);
    CHECK(batt_from_kind_byte(0) == BATT_NORMAL);
    CHECK(batt_from_kind_byte(99) == BATT_NORMAL);
}
