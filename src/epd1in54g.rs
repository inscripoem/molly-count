//! EPD 1.54" (G) four-color e-paper SPI driver, blocking variant.
//!
//! Ported verbatim from Waveshare `EPD_1in54g.cpp` (`reference/waveshare/`);
//! command sequences there are the source of truth. This module is esp-hal
//! glue (timing via `esp_hal::time`) and lives in the bin, so host-side
//! `cargo test --lib` never pulls esp-hal.

use embedded_hal::digital::{InputPin, OutputPin, StatefulOutputPin};
use embedded_hal::spi::SpiBus;

use esp_hal::time::{Duration, Instant};
use molly_count::framebuffer::{Framebuffer, WIDTH, HEIGHT};

/// Busy timeout for refresh / power-on. Official spec: full refresh ~20s.
const BUSY_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Debug)]
pub enum Error<S> {
    Spi(S),
    Timeout,
}

/// Blocking driver. Owns the SPI bus and the control pins;
/// CS is toggled manually around each phase, matching the C reference.
/// The power rail is NOT owned here: the caller drives it (active-low),
/// matching the vendor layering where DEV_Config owns the rail.
pub struct Epd1in54g<SPI, CS, DC, RST, BUSY> {
    spi: SPI,
    cs: CS,
    dc: DC,
    rst: RST,
    busy: BUSY,
}

impl<SPI, CS, DC, RST, BUSY> Epd1in54g<SPI, CS, DC, RST, BUSY>
where
    SPI: SpiBus,
    CS: OutputPin<Error = core::convert::Infallible>,
    DC: OutputPin<Error = core::convert::Infallible>,
    RST: OutputPin<Error = core::convert::Infallible>,
    BUSY: InputPin<Error = core::convert::Infallible>,
{
    pub fn new(spi: SPI, cs: CS, dc: DC, rst: RST, busy: BUSY) -> Self {
        Self {
            spi,
            cs,
            dc,
            rst,
            busy,
        }
    }

    fn wait_ms(ms: u64) {
        let start = Instant::now();
        while start.elapsed() < Duration::from_millis(ms) {
            core::hint::spin_loop();
        }
    }

    fn wait_busy_high(
        &mut self,
        led: &mut (impl OutputPin<Error = core::convert::Infallible> + StatefulOutputPin<Error = core::convert::Infallible>),
    ) -> Result<(), Error<SPI::Error>> {
        // Waveshare ReadBusyH: 100ms settle, then poll every 5ms until HIGH.
        // While waiting, blink the LED at ~4Hz (toggle every 25th 5ms poll):
        // the sealed-case "panel is working" signal.
        Self::wait_ms(100);
        let start = Instant::now();
        let mut polls = 0u32;
        while !self.busy.is_high().ok().unwrap_or(false) {
            polls += 1;
            if polls % 25 == 0 {
                let _ = led.toggle();
            }
            if start.elapsed() > BUSY_TIMEOUT {
                return Err(Error::Timeout);
            }
            Self::wait_ms(5);
        }
        Ok(())
    }

    fn cmd(&mut self, reg: u8) -> Result<(), Error<SPI::Error>> {
        let _ = self.dc.set_low();
        let _ = self.cs.set_low();
        let r = self.spi.write(&[reg]);
        let _ = self.cs.set_high();
        r.map_err(Error::Spi)
    }

    fn data(&mut self, byte: u8) -> Result<(), Error<SPI::Error>> {
        let _ = self.dc.set_high();
        let _ = self.cs.set_low();
        let r = self.spi.write(&[byte]);
        let _ = self.cs.set_high();
        r.map_err(Error::Spi)
    }

    fn reset(&mut self) {
        // Waveshare: high 200ms, low 2ms, high 200ms
        let _ = self.rst.set_high();
        Self::wait_ms(200);
        let _ = self.rst.set_low();
        Self::wait_ms(2);
        let _ = self.rst.set_high();
        Self::wait_ms(200);
    }

    /// Register init sequence, verbatim from `EPD_1IN54G_Init`.
    /// The caller must switch the power rail on before calling this;
    /// `led` blinks while the panel reports busy.
    pub fn init(
        &mut self,
        led: &mut (impl OutputPin<Error = core::convert::Infallible> + StatefulOutputPin<Error = core::convert::Infallible>),
    ) -> Result<(), Error<SPI::Error>> {
        self.reset();

        self.cmd(0x4D)?;
        self.data(0x78)?;

        self.cmd(0x00)?; // PSR
        self.data(0x0F)?;
        self.data(0x29)?;

        self.cmd(0x06)?; // BTST_P
        for b in [0x0D, 0x12, 0x30, 0x20, 0x19, 0x2A, 0x22] {
            self.data(b)?;
        }

        self.cmd(0x50)?; // CDI
        self.data(0x37)?;

        self.cmd(0x61)?; // TRES
        self.data((WIDTH / 256) as u8)?;
        self.data((WIDTH % 256) as u8)?;
        self.data((HEIGHT / 256) as u8)?;
        self.data((HEIGHT % 256) as u8)?;

        self.cmd(0xE9)?;
        self.data(0x01)?;

        self.cmd(0x30)?;
        self.data(0x08)?;

        self.cmd(0x04)?; // power on
        self.wait_busy_high(led)?;
        Ok(())
    }

    /// Stream the framebuffer (cmd 0x10) and trigger a full refresh (0x12).
    /// `led` blinks during the ~20s refresh wait.
    pub fn display(
        &mut self,
        fb: &Framebuffer,
        led: &mut (impl OutputPin<Error = core::convert::Infallible> + StatefulOutputPin<Error = core::convert::Infallible>),
    ) -> Result<(), Error<SPI::Error>> {
        self.cmd(0x10)?;
        let _ = self.dc.set_high();
        let _ = self.cs.set_low();
        for chunk in fb.packed().chunks(2048) {
            self.spi.write(chunk).map_err(Error::Spi)?;
        }
        let _ = self.cs.set_high();

        self.cmd(0x12)?; // DISPLAY_REFRESH
        self.data(0x00)?;
        self.wait_busy_high(led)?;
        Ok(())
    }

    /// Power off + deep sleep the panel (verbatim from `EPD_1IN54G_Sleep`).
    fn sleep(
        &mut self,
        led: &mut (impl OutputPin<Error = core::convert::Infallible> + StatefulOutputPin<Error = core::convert::Infallible>),
    ) -> Result<(), Error<SPI::Error>> {
        self.cmd(0x02)?; // POWER_OFF
        self.data(0x00)?;
        self.wait_busy_high(led)?;
        self.cmd(0x07)?; // DEEP_SLEEP
        self.data(0xA5)?;
        Ok(())
    }

    /// Vendor close sequence from `08_E_paper_test.ino`: panel sleep, then
    /// the mandatory settle ("DEV_Delay_ms(2000) //important, at least 2s")
    /// before anything cuts power, then reset low (`DEV_Module_Exit`).
    /// The caller switches the power rail off afterwards.
    pub fn shutdown(
        &mut self,
        led: &mut (impl OutputPin<Error = core::convert::Infallible> + StatefulOutputPin<Error = core::convert::Infallible>),
    ) -> Result<(), Error<SPI::Error>> {
        self.sleep(led)?;
        let _ = led.set_high(); // panel work done: blink stops before the settle
        Self::wait_ms(2000);
        let _ = self.rst.set_low();
        Ok(())
    }
}
