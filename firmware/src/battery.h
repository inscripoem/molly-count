// molly-count — battery state machine. Ported from src/battery.rs; pure
// logic, host-testable. Input: VBAT in mV (onboard 200K/200K divider on
// GPIO4 / ADC1_CH3). Hysteresis prevents flapping around a threshold.
#pragma once

#include <stdint.h>

typedef enum {
    BATT_NORMAL = 0,
    BATT_LOW = 1,
    BATT_CRITICAL = 2,
} BattState;

// Enter Low below this (≈10% remaining on a Li-ion discharge curve).
#define BATT_LOW_ENTER_MV 3550
// Enter Critical below this (refresh still safe above brownout territory).
#define BATT_CRIT_ENTER_MV 3350
// Leave Critical, back to Low, above this.
#define BATT_CRIT_EXIT_MV 3450
// Leave Low, back to Normal, above this.
#define BATT_LOW_EXIT_MV 3750

// Advance the state machine one wake cycle.
BattState battery_next_state(BattState prev, uint16_t vbat_mv);

// Screen-kind byte persisted in the flash record. 0xFF (erased flash) and
// any unknown value decode as Normal — records written before this feature
// never warned, and the panel indeed showed the normal screen.
uint8_t batt_kind_byte(BattState s);
BattState batt_from_kind_byte(uint8_t b);
