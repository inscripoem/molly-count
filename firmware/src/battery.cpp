// Ported from src/battery.rs — thresholds and transitions identical.
#include "battery.h"

BattState battery_next_state(BattState prev, uint16_t vbat_mv)
{
    switch (prev) {
    case BATT_NORMAL:
        if (vbat_mv < BATT_CRIT_ENTER_MV) return BATT_CRITICAL;
        if (vbat_mv < BATT_LOW_ENTER_MV) return BATT_LOW;
        return BATT_NORMAL;
    case BATT_LOW:
        if (vbat_mv < BATT_CRIT_ENTER_MV) return BATT_CRITICAL;
        if (vbat_mv >= BATT_LOW_EXIT_MV) return BATT_NORMAL;
        return BATT_LOW;
    case BATT_CRITICAL:
    default:
        if (vbat_mv >= BATT_LOW_EXIT_MV) return BATT_NORMAL;
        if (vbat_mv >= BATT_CRIT_EXIT_MV) return BATT_LOW;
        return BATT_CRITICAL;
    }
}

uint8_t batt_kind_byte(BattState s)
{
    switch (s) {
    case BATT_LOW: return 1;
    case BATT_CRITICAL: return 2;
    default: return 0;
    }
}

BattState batt_from_kind_byte(uint8_t b)
{
    switch (b) {
    case 1: return BATT_LOW;
    case 2: return BATT_CRITICAL;
    default: return BATT_NORMAL;
    }
}
