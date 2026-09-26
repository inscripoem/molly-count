// Ported from src/pcf85063.rs (which ported the vendor SensorLib register
// map) — byte-level behavior identical.
#include "pcf85063.h"

#include <Wire.h>

namespace {

const int ADDR = 0x51;
const uint8_t CTRL1 = 0x00;
const uint8_t SEC_REG = 0x04;
const uint8_t STOP_BIT = 0x02;

uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
uint8_t bcd2dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }

int write_regs(const uint8_t *buf, size_t len)
{
    Wire.beginTransmission(ADDR);
    Wire.write(buf, len);
    return (Wire.endTransmission() == 0) ? 1 : 0;
}

int write_then_read(const uint8_t *wr, size_t wlen, uint8_t *rd, size_t rlen)
{
    if (!write_regs(wr, wlen)) return 0;
    if (Wire.requestFrom((int)ADDR, (int)rlen) != (int)rlen) return 0;
    for (size_t i = 0; i < rlen; i++) rd[i] = Wire.read();
    return 1;
}

} // namespace

int pcf85063_read_datetime(DateTime *out, int *osc_stopped)
{
    uint8_t buf[7];
    uint8_t reg = SEC_REG;
    if (!write_then_read(&reg, 1, buf, 7)) return 0;
    if (osc_stopped) *osc_stopped = (buf[0] & 0x80) ? 1 : 0;
    uint8_t second = bcd2dec(buf[0] & 0x7F);
    uint8_t minute = bcd2dec(buf[1] & 0x7F);
    uint8_t hour = bcd2dec(buf[2] & 0x3F);
    uint8_t day = bcd2dec(buf[3] & 0x3F);
    uint8_t month = bcd2dec(buf[5] & 0x1F);
    int year = 2000 + bcd2dec(buf[6]);
    out->date.y = year;
    out->date.m = month;
    out->date.d = day;
    out->hh = hour;
    out->mm = minute;
    out->ss = second;
    return 1;
}

int pcf85063_set_datetime(const DateTime *dt)
{
    // stop clock (CTRL1 bit1)
    uint8_t ctrl1[1];
    if (!write_then_read(&CTRL1, 1, ctrl1, 1)) return 0;
    uint8_t stop_cmd[2] = { CTRL1, (uint8_t)(ctrl1[0] | STOP_BIT) };
    if (!write_regs(stop_cmd, 2)) return 0;

    // OSC_STOP flag (seconds bit7) clears on write; weekday unused, set 0
    const Date &d = dt->date;
    uint8_t regs[8] = {
        SEC_REG,
        (uint8_t)(dec2bcd(dt->ss) & 0x7F),
        dec2bcd(dt->mm),
        dec2bcd(dt->hh),
        dec2bcd(d.d),
        0x00,
        dec2bcd(d.m),
        dec2bcd((uint8_t)(d.y % 100)),
    };
    if (!write_regs(regs, 8)) return 0;

    // restart clock
    uint8_t start_cmd[2] = { CTRL1, (uint8_t)(ctrl1[0] & ~STOP_BIT) };
    return write_regs(start_cmd, 2);
}
