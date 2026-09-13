#![no_std]
#![no_main]

use embedded_graphics::geometry::Point;
use embedded_graphics::mono_font::{ascii::FONT_6X12, MonoTextStyle};
use embedded_graphics::prelude::Drawable as _;
use embedded_graphics::text::Text;
use esp_backtrace as _;
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::i2c::master::{Config as I2cConfig, I2c};
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::time::{Duration, Instant, Rate};
use esp_hal::usb::usb_serial_jtag::UsbSerialJtag;
use esp_storage::FlashStorage;
use molly_count::civil::{parse_date, parse_hms, Date, DateTime};
use molly_count::framebuffer::{EpdColor, Framebuffer};
use molly_count::store::{Record, RECORD_LEN, SECTOR};

mod console;
mod epd1in54g;
mod pcf85063;
use console::Console;
use epd1in54g::Epd1in54g;
use pcf85063::Pcf85063;

esp_bootloader_esp_idf::esp_app_desc!();

// 10KB framebuffer: too big for the default stack, lives in static RAM.
static mut FRAMEBUFFER: Framebuffer = Framebuffer::new();

/// Used when the flash record is blank (first boot ever).
const DEFAULT_ANNIVERSARY: Date = Date::new(2025, 2, 14);

fn load_record(flash: &mut FlashStorage) -> Option<Record> {
    let mut buf = [0u8; RECORD_LEN];
    flash.read(SECTOR, &mut buf).ok()?;
    Record::unpack(&buf)
}

fn save_record(flash: &mut FlashStorage, record: &Record) {
    let packed = record.pack();
    if let Err(e) = flash.write(SECTOR, &packed) {
        esp_println::println!("flash write failed: {:?}", e);
    }
}

fn draw_day_screen(fb: &mut Framebuffer, anniv: &Date, days: i64, today: &Date) {
    use core::fmt::Write as _;
    // static art (header, 第/天 frame, hearts, jasmine) + red day digits
    molly_count::assets::draw_day_background(fb, days);

    // runtime bottom row: anniversary + today, ASCII 6x12
    let mut since = heapless::String::<32>::new();
    let _ = write!(since, "SINCE {:04}.{:02}.{:02}", anniv.y, anniv.m, anniv.d);
    Text::new(
        since.as_str(),
        Point::new(8, 192),
        MonoTextStyle::new(&FONT_6X12, EpdColor::Black),
    )
    .draw(fb)
    .ok();

    let mut today_str = heapless::String::<32>::new();
    let _ = write!(today_str, "{:04}.{:02}.{:02}", today.y, today.m, today.d);
    Text::new(
        today_str.as_str(),
        Point::new(134, 192),
        MonoTextStyle::new(&FONT_6X12, EpdColor::Black),
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

    let mut flash = FlashStorage::new(p.FLASH);
    let mut record = load_record(&mut flash).unwrap_or_else(|| {
        esp_println::println!("flash record blank, using placeholder");
        Record::placeholder()
    });

    // ---- console phase (USB-CDC), max ~15s ----
    let mut console = Console::new(UsbSerialJtag::new(p.USB_DEVICE));
    console.print(
        "molly-count console\r\nSETDATE y-m-d h:m:s | SETANNIV y-m-d | GET\r\n",
    );
    let console_start = Instant::now();
    let mut save_needed = false;
    while console_start.elapsed() < Duration::from_secs(15) {
        let Some(line) = console.read_line(3000) else { break };
        let mut parts = line.split_ascii_whitespace();
        match parts.next() {
            Some("SETDATE") => {
                let d = parts.next().and_then(parse_date);
                let t = parts.next().and_then(parse_hms);
                match (d, t) {
                    (Some(date), Some((hh, mm, ss))) => {
                        match rtc.set_datetime(DateTime::new(date, hh, mm, ss)) {
                            Ok(()) => console.print("OK DATE\r\n"),
                            Err(_) => console.print("ERR RTC\r\n"),
                        }
                    }
                    _ => console.print("ERR usage: SETDATE y-m-d h:m:s\r\n"),
                }
            }
            Some("SETANNIV") => match parts.next().and_then(parse_date) {
                Some(date) => {
                    record.anniv = date;
                    save_needed = true;
                    console.print("OK ANNIV\r\n");
                }
                None => console.print("ERR usage: SETANNIV y-m-d\r\n"),
            },
            Some("GET") => {
                if let Ok(now) = rtc.datetime() {
                    let mut s = heapless::String::<96>::new();
                    use core::fmt::Write as _;
                    let _ = write!(
                        s,
                        "ANNIV {:04}-{:02}-{:02} LAST {}\r\n",
                        record.anniv.y, record.anniv.m, record.anniv.d, record.last_shown_days
                    );
                    console.print(s.as_str());
                }
            }
            _ => console.print("ERR?\r\n"),
        }
    }
    if save_needed {
        save_record(&mut flash, &record);
        console.print("SAVED\r\n");
    }

    // ---- day computation ----
    let now = rtc.datetime().unwrap();
    let days = record.anniv.days_since(now.date);
    esp_println::println!(
        "rtc: {:04}-{:02}-{:02} {:02}:{:02}:{:02}, days: {}",
        now.date.y,
        now.date.m,
        now.date.d,
        now.hh,
        now.mm,
        now.ss,
        days
    );

    let mut epd = Epd1in54g::new(spi, cs, dc, rst, busy, pwr);

    if days != record.last_shown_days {
        let fb = unsafe { &mut *(&raw mut FRAMEBUFFER) };
        draw_day_screen(fb, &record.anniv, days, &now.date);

        esp_println::println!("day changed -> full refresh (~20s)...");
        let t0 = Instant::now();
        epd.init().unwrap();
        epd.display(fb).unwrap();
        esp_println::println!(
            "refresh done in {} ms, panel sleep + rail off",
            t0.elapsed().as_millis()
        );
        epd.sleep().unwrap();
        epd.power_off();

        record.last_shown_days = days;
        save_record(&mut flash, &record);
        console.print("DISPLAY UPDATED\r\n");
    } else {
        esp_println::println!("day unchanged, skip refresh");
        console.print("UNCHANGED\r\n");
    }

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
