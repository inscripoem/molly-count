#![no_std]
#![no_main]

use embedded_graphics::geometry::Point;
use embedded_graphics::mono_font::{ascii::FONT_6X12, ascii::FONT_9X15_BOLD, MonoTextStyle};
use embedded_graphics::text::{Baseline, TextStyleBuilder};
use embedded_graphics::prelude::Drawable as _;
use embedded_graphics::text::Text;
use esp_backtrace as _;
use esp_hal::analog::adc::{Adc, AdcCalLine, AdcConfig, AdcPin, Attenuation};
use esp_hal::gpio::{Input, InputConfig, Level, Output, OutputConfig, Pull};
use esp_hal::i2c::master::{Config as I2cConfig, I2c};
use esp_hal::rtc_cntl::sleep::{LowPower, RtcSleepConfig};
use esp_hal::spi::master::{Config as SpiConfig, Spi};
use esp_hal::time::{Duration, Instant, Rate};
use esp_hal::usb::usb_serial_jtag::UsbSerialJtag;
use esp_storage::FlashStorage;
use molly_count::battery::{self, BattState};
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

fn draw_screen(fb: &mut Framebuffer, state: BattState, anniv: &Date, days: i64, today: &Date) {
    use core::fmt::Write as _;
    // static art (per battery state) + black day digits
    molly_count::assets::draw_day_background(fb, state, days);

    // the charge warning screen is fully static: no digits, no date row
    if state == BattState::Critical {
        return;
    }

    // top-right date, right-aligned (chip slots live in the static background)
    let mut date = heapless::String::<16>::new();
    let _ = write!(date, "{:04}.{:02}.{:02}", today.y, today.m, today.d);
    Text::new(
        date.as_str(),
        Point::new(10, 18),
        MonoTextStyle::new(&FONT_6X12, EpdColor::Black),
    )
    .draw(fb)
    .ok();

    let mut since = heapless::String::<24>::new();
    let _ = write!(since, "SINCE {:04}.{:02}.{:02}", anniv.y, anniv.m, anniv.d);
    // Text::new's default baseline is Alphabetic (position.y = BASELINE);
    // Middle anchors the text's vertical center to position.y = chip center
    Text::with_baseline(
        since.as_str(),
        Point::new(21, 171),
        MonoTextStyle::new(&FONT_9X15_BOLD, EpdColor::Black),
        Baseline::Middle,
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

    // Power rails per vendor board_power_bsp:
    //   Audio (GPIO42): 0=on, 1=off  -> drive HIGH (off, unused in this app)
    //   VBAT path (GPIO17): 1=on, 0=off -> drive HIGH to latch system power on
    //     (the vendor firmware does this first thing at boot; GPIO17 low =
    //     "shutdown", never used here)
    let mut audio_pwr = Output::new(p.GPIO42, Level::High, OutputConfig::default());
    let mut vbat_path = Output::new(p.GPIO17, Level::High, OutputConfig::default());

    // Battery voltage: VBAT via 200K/200K divider on GPIO4 (ADC1_CH3),
    // VBAT = VADC x 2 per official docs.
    let mut adc_config = AdcConfig::new();
    let mut batt_pin: AdcPin<_, _, AdcCalLine<_>> =
        adc_config.enable_pin_with_cal(p.GPIO4, Attenuation::_11dB);
    let mut adc = Adc::new(p.ADC1, adc_config);
    let vbat_mv = (adc.read_blocking(&mut batt_pin) as u32 * 2).min(5000) as u16;

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
        "molly-count console\r\nSETDATE y-m-d h:m:s | SETANNIV y-m-d | GET | SLEEP s | BATTTEST LOW|CRIT\r\n",
    );
    let console_start = Instant::now();
    let mut save_needed = false;
    let mut sleep_override: Option<u64> = None;
    let mut batttest: Option<BattState> = None;
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
            Some("SLEEP") => match parts.next().and_then(|s| s.parse::<u64>().ok()) {
                Some(secs) => {
                    sleep_override = Some(secs);
                    console.print("OK SLEEP\r\n");
                }
                None => console.print("ERR usage: SLEEP seconds\r\n"),
            },
            Some("BATTTEST") => match parts.next() {
                Some("LOW") => {
                    batttest = Some(BattState::Low);
                    console.print("OK BATTTEST LOW\r\n");
                }
                Some("CRIT") => {
                    batttest = Some(BattState::Critical);
                    console.print("OK BATTTEST CRIT\r\n");
                }
                _ => console.print("ERR usage: BATTTEST LOW|CRIT\r\n"),
            },
            Some("GET") => {
                if let Ok(now) = rtc.datetime() {
                    let mut s = heapless::String::<128>::new();
                    use core::fmt::Write as _;
                    let _ = write!(
                        s,
                        "ANNIV {:04}-{:02}-{:02} LAST {} VBAT {}mV KIND {}\r\n",
                        record.anniv.y,
                        record.anniv.m,
                        record.anniv.d,
                        record.last_shown_days,
                        vbat_mv,
                        record.screen_kind
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

    // ---- day + battery state ----
    let now = rtc.datetime().unwrap();
    let days = record.anniv.day_number(now.date); // anniversary = day 1
    let prev_state = BattState::from_kind_byte(record.screen_kind);
    let state = batttest.unwrap_or_else(|| battery::next_state(prev_state, vbat_mv));
    let kind = state.kind_byte();
    esp_println::println!(
        "rtc: {:04}-{:02}-{:02} {:02}:{:02}:{:02}, days: {}, vbat: {}mV, batt: {:?}",
        now.date.y,
        now.date.m,
        now.date.d,
        now.hh,
        now.mm,
        now.ss,
        days,
        vbat_mv,
        state
    );

    let mut epd = Epd1in54g::new(spi, cs, dc, rst, busy, pwr);

    let screen_changed = kind != record.screen_kind;
    let needs_refresh = match state {
        // charge warning is static; ignore day changes while on it
        BattState::Critical => screen_changed,
        _ => screen_changed || days != record.last_shown_days,
    };

    if needs_refresh {
        let fb = unsafe { &mut *(&raw mut FRAMEBUFFER) };
        draw_screen(fb, state, &record.anniv, days, &now.date);

        esp_println::println!("screen update -> full refresh (~20s)...");
        let t0 = Instant::now();
        epd.init().unwrap();
        epd.display(fb).unwrap();
        esp_println::println!(
            "refresh done in {} ms, panel sleep + rail off",
            t0.elapsed().as_millis()
        );
        epd.sleep().unwrap();
        epd.power_off();

        // BATTTEST screens are demos: don't persist them as real state
        if batttest.is_none() {
            record.last_shown_days = days;
            record.screen_kind = kind;
            save_record(&mut flash, &record);
        }
        console.print("DISPLAY UPDATED\r\n");
    } else {
        esp_println::println!("nothing changed, skip refresh");
        console.print("UNCHANGED\r\n");
    }

    // two quick blinks = update cycle finished, about to sleep
    let mut led = Output::new(p.GPIO3, Level::High, OutputConfig::default());
    for _ in 0..4 {
        led.toggle();
        let start = Instant::now();
        while start.elapsed() < Duration::from_millis(150) {
            core::hint::spin_loop();
        }
    }

    // ---- rails before sleep: EPD off (already), audio off, VBAT path on ----
    let _ = audio_pwr.set_high();
    let _ = vbat_path.set_high();

    // ---- deep sleep until WAKE_HOUR (or SLEEP override for testing) ----
    const WAKE_HOUR: u8 = 6;
    let now2 = rtc.datetime().unwrap();
    let secs = sleep_override.unwrap_or_else(|| seconds_until_next(WAKE_HOUR, now2));
    esp_println::println!("deep sleep for {} s (wake at {:02}:00)", secs, WAKE_HOUR);
    console.print("SLEEPING\r\n");

    let mut lp = LowPower::new(p.LPWR);
    lp.set_wakeup_deadline(Instant::now() + Duration::from_secs(secs));
    lp.sleep_deep(RtcSleepConfig::default());
}

fn seconds_until_next(hour: u8, now: DateTime) -> u64 {
    const DAY: u64 = 24 * 3600;
    let now_secs = now.hh as u64 * 3600 + now.mm as u64 * 60 + now.ss as u64;
    let target = hour as u64 * 3600;
    if now_secs < target {
        target - now_secs
    } else {
        DAY - (now_secs - target)
    }
}
