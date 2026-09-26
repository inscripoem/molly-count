// molly-count — PCF85063 RTC driver (I2C address 0x51) over Wire.
// Register map ported from Waveshare SensorLib (reference/waveshare/),
// semantics identical to the legacy Rust src/pcf85063.rs: 7 BCD datetime
// registers starting at 0x04.
#pragma once

#include "civil.h"

// 1 ok / 0 I2C error. osc_stopped (may be NULL) reports the OSC_STOP flag
// (seconds bit 7: set when the oscillator stalled since the last clear).
int pcf85063_read_datetime(DateTime *out, int *osc_stopped);

// Write datetime: stop the clock, load registers, restart.
int pcf85063_set_datetime(const DateTime *dt);
