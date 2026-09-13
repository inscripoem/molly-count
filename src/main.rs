#![no_std]
#![no_main]

use embedded_graphics::geometry::Point;
use embedded_graphics::mono_font::{ascii::FONT_10X20, MonoTextStyle};
use embedded_graphics::text::Text;
use esp_backtrace as _;
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::i2c::master::{Config as I2cConfig, I2c};
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::time::{Duration, Instant, Rate};
use molly_count::civil::{Date, DateTime};
use molly_count::framebuffer::{EpdColor, Framebuffer};

mod epd1in54g;
mod pcf85063;
use embedded_graphics::prelude::Drawable as _;
use epd1in54g::Epd1in54g;
use pcf85063::Pcf85063;

esp_bootloader_esp_idf::esp_app_desc!();

// 10KB framebuffer: too big for the default stack, lives in static RAM.
static mut FRAMEBUFFER: Framebuffer = Framebuffer::new();

/// Placeholder until the serial console (M3b) writes it to NVS.
const ANNIVERSARY: Date = Date::new(2025, 2, 14);

fn draw_day_screen(fb: &mut Framebuffer, days: i64, today: &Date) {
    use core::fmt::Write as _;
    fb.clear(EpdColor::White);

    let mut since = heapless::String::<32>::new();
    let _ = write!(since, "SINCE {:04}-{:02}-{:02}", ANNIVERSARY.y, ANNIVERSARY.m, ANNIVERSARY.d);
    Text::new(
        since.as_str(),
        Point::new(20, 40),
        MonoTextStyle::new(&FONT_10X20, EpdColor::Black),
    )
    .draw(fb)
    .ok();

    let mut days_str = heapless::String::<16>::new();
    let _ = write!(days_str, "DAY {}", days);
    Text::new(
        days_str.as_str(),
        Point::new(20, 110),
        MonoTextStyle::new(&FONT_10X20, EpdColor::Red),
    )
    .draw(fb)
    .ok();

    let mut today_str = heapless::String::<32>::new();
    let _ = write!(today_str, "TODAY {:04}-{:02}-{:02}", today.y, today.m, today.d);
    Text::new(
        today_str.as_str(),
        Point::new(20, 170),
        MonoTextStyle::new(&FONT_10X20, EpdColor::Yellow),
    )
    .draw(fb)
    .ok();
}

#[esp_hal::main]
fn main() -> ! {
    let p = esp_hal::init(esp_hal::Config::default());

    let spi = Spi::new(
        p.SPI2,
        SpiConfig::default().with_frequency(Rate::from_mhz(4)),
    )
    .unwrap()
    .with_sck(p.GPIO12)
    .with_mosi(p.GPIO13);

    // Panel wiring per official docs: PWR=6 BUSY=8 RST=9 DC=10 CS=11 SCLK=12 SDI=13
    let cs = Output::new(p.GPIO11, Level::High, OutputConfig::default());
    let dc = Output::new(p.GPIO10, Level::Low, OutputConfig::default());
    let rst = Output::new(p.GPIO9, Level::High, OutputConfig::default());
    // Pull-down so a floating/unpowered BUSY reads LOW -> wait times out loudly
    // instead of passing spuriously.
    let busy = Input::new(p.GPIO8, InputConfig::default().with_pull(Pull::Down));
    let pwr = Output::new(p.GPIO6, Level::High, OutputConfig::default()); // active-low rail

    // PCF85063 on I2C0: SDA=47 SCL=48 (official pin table)
    let i2c = I2c::new(
        p.I2C0,
        I2cConfig::default().with_frequency(Rate::from_khz(100)),
    )
    .unwrap()
    .with_sda(p.GPIO47)
    .with_scl(p.GPIO48);
    let mut rtc = Pcf85063::new(i2c);

    // M3a test hook: seed the RTC with a known time on every boot so the
    // I2C write path is exercised. Replaced by the serial console in M3b.
    rtc.set_datetime(DateTime::new(Date::new(2026, 9, 14), 8, 30, 0))
        .unwrap();

    let now = rtc.datetime().unwrap();
    esp_println::println!(
        "rtc: {:04}-{:02}-{:02} {:02}:{:02}:{:02}",
        now.date.y,
        now.date.m,
        now.date.d,
        now.hh,
        now.mm,
        now.ss
    );
    let days = ANNIVERSARY.days_since(now.date);
    esp_println::println!("days since {:?}: {}", ANNIVERSARY, days);

    let mut epd = Epd1in54g::new(spi, cs, dc, rst, busy, pwr);

    let fb = unsafe { &mut *(&raw mut FRAMEBUFFER) };
    draw_day_screen(fb, days, &now.date);

    esp_println::println!("epd init + display (full refresh, ~20s)...");
    let t0 = Instant::now();
    epd.init().unwrap();
    epd.display(fb).unwrap();
    esp_println::println!(
        "refresh done in {} ms, panel sleep + rail off",
        t0.elapsed().as_millis()
    );

    epd.sleep().unwrap();
    epd.power_off();

    // Slow blink = update cycle finished.
    let mut led = Output::new(p.GPIO3, Level::High, OutputConfig::default());
    loop {
        led.toggle();
        let start = Instant::now();
        while start.elapsed() < Duration::from_millis(500) {
            core::hint::spin_loop();
        }
    }
}
