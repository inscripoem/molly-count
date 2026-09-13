//! PCF85063 RTC driver (I2C address 0x51) over esp-hal I2C.
//!
//! Register map ported from Waveshare SensorLib (`reference/waveshare/`);
//! 7 BCD-coded datetime registers starting at 0x04.

use embedded_hal::i2c::ErrorType;
use esp_hal::i2c::master::I2c;
use molly_count::civil::{Date, DateTime};

const ADDR: u8 = 0x51;
const CTRL1: u8 = 0x00;
const SEC_REG: u8 = 0x04;
const STOP_BIT: u8 = 0x02;

type I2cBus = I2c<'static, esp_hal::Blocking>;
type BusError = <I2cBus as ErrorType>::Error;

fn dec2bcd(v: u8) -> u8 {
    ((v / 10) << 4) | (v % 10)
}

fn bcd2dec(v: u8) -> u8 {
    ((v >> 4) * 10) + (v & 0x0F)
}

pub struct Pcf85063 {
    i2c: I2cBus,
}

impl Pcf85063 {
    pub fn new(i2c: I2cBus) -> Self {
        Self { i2c }
    }

    /// Read the 7 datetime registers and decode (24-hour mode).
    pub fn datetime(&mut self) -> Result<DateTime, BusError> {
        let mut buf = [0u8; 7];
        self.i2c.write_read(ADDR, &[SEC_REG], &mut buf)?;
        let second = bcd2dec(buf[0] & 0x7F);
        let minute = bcd2dec(buf[1] & 0x7F);
        let hour = bcd2dec(buf[2] & 0x3F);
        let day = bcd2dec(buf[3] & 0x3F);
        let month = bcd2dec(buf[5] & 0x1F);
        let year = 2000 + bcd2dec(buf[6]) as i32;
        Ok(DateTime::new(Date::new(year, month, day), hour, minute, second))
    }

    /// Write datetime: stop the clock, load registers, restart.
    pub fn set_datetime(&mut self, dt: DateTime) -> Result<(), BusError> {
        // stop clock (CTRL1 bit1)
        let mut ctrl1 = [0u8; 1];
        self.i2c.write_read(ADDR, &[CTRL1], &mut ctrl1)?;
        self.i2c.write(ADDR, &[CTRL1, ctrl1[0] | STOP_BIT])?;

        // OSC_STOP flag (seconds bit7) clears on write; weekday unused, set 0
        let d = dt.date;
        let regs = [
            SEC_REG,
            dec2bcd(dt.ss) & 0x7F,
            dec2bcd(dt.mm),
            dec2bcd(dt.hh),
            dec2bcd(d.d),
            0x00, // weekday
            dec2bcd(d.m),
            dec2bcd((d.y % 100) as u8),
        ];
        self.i2c.write(ADDR, &regs)?;

        // restart clock
        self.i2c.write(ADDR, &[CTRL1, ctrl1[0] & !STOP_BIT])?;
        Ok(())
    }
}
