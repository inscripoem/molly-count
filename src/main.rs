//! molly-count: a four-color e-paper love-day counter.
//! Boot flow: console window -> RTC -> battery state -> conditional
//! full refresh -> deep sleep until 06:00.
#![no_std]
#![no_main]

use embedded_graphics::geometry::Point;
use embedded_graphics::mono_font::{ascii::FONT_6X12, ascii::FONT_9X15_BOLD, MonoTextStyle};
use embedded_graphics::text::Baseline;
use embedded_graphics::prelude::Drawable as _;
use embedded_graphics::text::Text;
use esp_backtrace as _;
use esp_hal::analog::adc::{Adc, AdcCalLine, AdcConfig, AdcPin, Attenuation};
use esp_hal::gpio::{Event, Input, InputConfig, Level, Output, OutputConfig, Pull, WakeupConfig};
use esp_hal::i2c::master::{Config as I2cConfig, I2c};
use esp_hal::rtc_cntl::{wakeup_cause, Rtc, WakeupSource};
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

/// Wake evidence: 1 = EXT0 (PWR), 2 = EXT1 (BOOT), 3 = per-pin GPIO.
/// Written only on real button wakes; RTC fast RAM keeps the value across
/// deep sleep AND across resets, so a press is provable long after the fact
/// via `GET`. Full power loss (battery latch failure!) reinitializes it to 0.
#[esp_hal::ram(unstable(rtc_fast, persistent))]
static mut LAST_WAKE: u8 = 0;

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

    // Sleep-timer probe: the RTC slow clock drives the deep-sleep alarm.
    let rtc_meta = Rtc::new(unsafe { esp_hal::peripherals::RTC_TIMER::steal() });
    let uptime_at_boot = rtc_meta.time_since_power_up();
    esp_println::println!("rtc slow uptime at boot: {} ms", uptime_at_boot.as_millis());

    // green LED (GPIO3, low-active): OFF while idle or asleep, BLINKING
    // while the panel is working (init/refresh ~20s). Solid light is never
    // a normal state; on a sealed device the blink is the "not stuck" signal.
    // Held high (off) through deep sleep like every other pad.
    let mut led = Output::new(p.GPIO3, Level::High, OutputConfig::default());

    // PWR button (GPIO18): idle LOW (board pull-down), pressed = HIGH.
    // It is the ONLY wake-armed button: a single HIGH pin takes the EXT1
    // path (proven). Arming BOOT (GPIO0, pressed = LOW) as well would split
    // the pins across EXT1+EXT0 — the S3 EXT0 path does not fire in practice
    // (verified 2026-09-24: BOOT wakes, PWR with identical arm registers
    // does not), and PWR is the button that must wake the device.
    let mut pwr_btn = Input::new(p.GPIO18, InputConfig::default().with_pull(Pull::Down));
    pwr_btn
        .apply_wakeup_config(&WakeupConfig::default().with_low_power_path(true))
        .unwrap();
    pwr_btn.listen(Event::HighLevel);
    // BOOT (GPIO0): plain input, readable via the console BTN command.
    let mut boot_btn = Input::new(p.GPIO0, InputConfig::default().with_pull(Pull::Up));

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
    // EPD power rail (GPIO6, active-low) is owned here, not by the panel
    // driver: before deep sleep it must be driven AND pad-held like every
    // other rail.
    let mut epd_rail = Output::new(p.GPIO6, Level::High, OutputConfig::default());

    // Power rails per vendor board_power_bsp:
    //   Audio (GPIO42): 0=on, 1=off  -> drive HIGH (off, unused in this app)
    //   VBAT path (GPIO17): 1=on, 0=off -> drive HIGH to latch system power on
    //     (the vendor firmware does this first thing at boot; GPIO17 low =
    //     "shutdown", never used here)
    // GPIO3/6/17 are LP pads: without a pad hold, deep sleep floats them once
    // the digital domain powers down — and a floating GPIO17 opens the
    // battery latch, so the board loses power in its sleep.
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
    // button-level edges during the window: diagnoses whether the PWR
    // button (BAT_KEY) can change GPIO18 at all in the current power setup
    let mut last_pwr = pwr_btn.is_high();
    let mut last_boot = boot_btn.is_high();
    esp_println::println!(
        "btn idle: PWR(GPIO18)={} BOOT(GPIO0)={}",
        if last_pwr { "HIGH" } else { "LOW" },
        if last_boot { "HIGH" } else { "LOW" },
    );
    let mut save_needed = false;
    let mut sleep_override: Option<u64> = None;
    let mut batttest: Option<BattState> = None;
    while console_start.elapsed() < Duration::from_secs(15) {
        // slow heartbeat while the console window is open: the sealed-case
        // "awake and waiting" signal (fast blink = panel busy, off = asleep).
        // A button wake must show light within a second, or it reads as
        // "no reaction".
        let _ = led.toggle();
        let pwr_now = pwr_btn.is_high();
        let boot_now = boot_btn.is_high();
        if pwr_now != last_pwr || boot_now != last_boot {
            esp_println::println!(
                "btn edge: PWR(GPIO18)={} BOOT(GPIO0)={}",
                if pwr_now { "HIGH" } else { "LOW" },
                if boot_now { "HIGH" } else { "LOW" },
            );
            last_pwr = pwr_now;
            last_boot = boot_now;
        }
        // a full 15 s window: idle ticks just blink the LED instead of
        // ending the phase early
        let Some(line) = console.read_line(600) else { continue };
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
            Some("BTN") => {
                let mut s = heapless::String::<48>::new();
                use core::fmt::Write as _;
                let _ = write!(
                    s,
                    "PWR(GPIO18) {}\r\nBOOT(GPIO0) {}\r\n",
                    if pwr_btn.is_high() { "HIGH" } else { "LOW" },
                    if boot_btn.is_high() { "HIGH" } else { "LOW" },
                );
                console.print(s.as_str());
            }
            Some("GET") => {
                if let Ok((_now, osc_stopped)) = rtc.datetime() {
                    let mut s = heapless::String::<160>::new();
                    use core::fmt::Write as _;
                    let _ = write!(
                        s,
                        "ANNIV {:04}-{:02}-{:02} LAST {} VBAT {}mV KIND {} RTC_STOPPED {} WAKE {}\r\n",
                        record.anniv.y,
                        record.anniv.m,
                        record.anniv.d,
                        record.last_shown_days,
                        vbat_mv,
                        record.screen_kind,
                        osc_stopped,
                        unsafe { LAST_WAKE },
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
    let (now, _osc_stopped) = rtc.datetime().unwrap();
    // a button wake always refreshes: the visible flicker is the proof of life.
    // The two buttons land on different low-power paths (one level group on
    // EXT1, the odd level on EXT0), so every GPIO-flavoured cause counts.
    let wake_cause = wakeup_cause();
    let button_wake = wake_cause.contains(WakeupSource::Gpio)
        || wake_cause.contains(WakeupSource::Ext0)
        || wake_cause.contains(WakeupSource::Ext1);
    // forensics: the decoded wake sources (empty on S3 — see below), the
    // raw cause register, plus what the previous sleep armed in the LP wake
    // registers (they survive the wake reset).
    let lpwr = esp_hal::peripherals::LPWR::regs();
    let ext1_fired = lpwr.ext_wakeup1_status().read().ext_wakeup1_status().bits();
    {
        use core::fmt::Write as _;
        let mut srcs = heapless::String::<80>::new();
        for s in wake_cause.iter() {
            let _ = write!(srcs, "{s:?} ");
        }
        let rtc_io = esp_hal::peripherals::RTC_IO::regs();
        let conf = lpwr.ext_wakeup_conf().read();
        esp_println::println!(
            "wake cause [{}]| raw cause {:#010x} | prev arms: ext0 sel {} lv {} | ext1 sel {:#010x} lv {} | ext1 fired {:#010x}",
            srcs.as_str(),
            lpwr.slp_wakeup_cause().read().wakeup_cause().bits(),
            rtc_io.ext_wakeup0().read().sel().bits(),
            if conf.ext_wakeup0_lv().bit() { "high" } else { "low" },
            lpwr.ext_wakeup1().read().sel().bits(),
            if conf.ext_wakeup1_lv().bit() { "high" } else { "low" },
            ext1_fired,
        );
    }

    // esp-hal's wakeup_cause() decodes EMPTY on the S3 even after a real
    // EXT1 button wake (verified 2026-09-24: ext1 fired 0x1 with an empty
    // cause set), so the button decision comes from the hardware status
    // register instead: any EXT1 status bit is a button wake. Timer wakes
    // leave it zero, and timer wakes deliberately do not touch LAST_WAKE:
    // button evidence survives until the next button event or power loss.
    let button_wake = button_wake || ext1_fired != 0;
    if button_wake {
        unsafe {
            LAST_WAKE = if wake_cause.contains(WakeupSource::Ext0) {
                1
            } else if wake_cause.contains(WakeupSource::Ext1) || ext1_fired != 0 {
                2
            } else {
                3
            };
        }
    }
    let days = record.anniv.day_number(now.date); // anniversary = day 1
    let prev_state = BattState::from_kind_byte(record.screen_kind);
    let state = batttest.unwrap_or_else(|| battery::next_state(prev_state, vbat_mv));
    let kind = state.kind_byte();
    esp_println::println!(
        "rtc: {:04}-{:02}-{:02} {:02}:{:02}:{:02}, days: {}, vbat: {}mV, batt: {:?}, button_wake: {}, last: {}, kind: {}, wake_ev: {}",
        now.date.y,
        now.date.m,
        now.date.d,
        now.hh,
        now.mm,
        now.ss,
        days,
        vbat_mv,
        state,
        button_wake,
        record.last_shown_days,
        record.screen_kind,
        unsafe { LAST_WAKE },
    );

    let mut epd = Epd1in54g::new(spi, cs, dc, rst, busy);

    let screen_changed = kind != record.screen_kind;
    // button wake ALWAYS refreshes: on a sealed cased device the visible
    // flicker is the only observable proof that it woke up and is alive
    let needs_refresh = button_wake
        || match state {
            // charge warning is static; ignore day changes while on it
            BattState::Critical => screen_changed,
            _ => screen_changed || days != record.last_shown_days,
        };

    if needs_refresh {
        let fb = unsafe { &mut *(&raw mut FRAMEBUFFER) };
        draw_screen(fb, state, &record.anniv, days, &now.date);

        esp_println::println!("screen update -> full refresh (~20s)...");
        let t0 = Instant::now();
        let _ = epd_rail.set_low(); // active-low rail on, before panel reset
        epd.init(&mut led).unwrap();
        epd.display(fb, &mut led).unwrap();
        esp_println::println!(
            "refresh done in {} ms, panel sleep + rail off",
            t0.elapsed().as_millis()
        );
        epd.shutdown(&mut led).unwrap();

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

    // awake phase over: LED off = sleeping
    led.set_high();

    // ---- rails before sleep: EPD off, audio off, VBAT path latched on ----
    // Every pad is held at its driven level: deep sleep powers the digital
    // domain down, and an unheld pad floats. GPIO3/6/17 are LP pads, and a
    // floating GPIO17 opens the battery latch = the board never wakes again.
    let _ = epd_rail.set_high();
    let _ = audio_pwr.set_high();
    let _ = vbat_path.set_high();
    epd_rail.set_pad_hold(true);
    audio_pwr.set_pad_hold(true);
    vbat_path.set_pad_hold(true);
    led.set_pad_hold(true);

    // ---- deep sleep until WAKE_HOUR (or SLEEP override for testing) ----
    const WAKE_HOUR: u8 = 6;
    let (now2, _) = rtc.datetime().unwrap();
    let secs = sleep_override.unwrap_or_else(|| seconds_until_next(WAKE_HOUR, now2));
    esp_println::println!("deep sleep for {} s (wake at {:02}:00)", secs, WAKE_HOUR);
    console.print("SLEEPING\r\n");

    let mut lp = LowPower::new(p.LPWR);
    lp.set_wakeup_deadline(Instant::now() + Duration::from_secs(secs));
    // Diagnostic: the deep-sleep alarm runs on the RTC slow clock. If the
    // two uptime reads diverge by ~the awake phase length, the clock runs.
    esp_println::println!(
        "rtc slow uptime: {} ms at boot, {} ms at sleep entry",
        uptime_at_boot.as_millis(),
        rtc_meta.time_since_power_up().as_millis()
    );
    // esp-hal (1.2.2) has no USB-Serial-JTAG sleep preparation on the S3 —
    // IDF calls usb_uart_jtag_sleep_prepare() here. Register semantics are
    // copied from that function: phy_sel=1 puts the PHY in ADC mode so DP/D-
    // leave the outside world, then the bridge and pull-ups go away. A half
    // prepared USJ leaves the pad in a marginal state the host can attach to
    // but not enumerate (-71 descriptor errors).
    esp_hal::peripherals::USB_DEVICE::regs().conf0().modify(|_, w| {
        w.phy_sel()
            .set_bit()
            .usb_jtag_bridge_en()
            .clear_bit()
            .pullup_value()
            .clear_bit()
            .dp_pullup()
            .clear_bit()
            .pad_pull_override()
            .set_bit()
    });
    lp.sleep_deep(RtcSleepConfig::deep());
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
