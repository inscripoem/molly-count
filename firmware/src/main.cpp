// molly-count — ESP32-S3 four-color e-paper love-day counter.
// PlatformIO + Arduino port of the legacy Rust/esp-hal firmware; the Rust
// sources (git history) are the behavior spec. Boot flow: battery latch ->
// console window (15s) -> RTC -> day count -> battery state -> full refresh
// -> deep sleep until 06:00.
//
// Hardware pipeline copied from the Waveshare examples (reference/waveshare/
// + reference/vendor_power/): panel driver verbatim, vendor ext0 deep-sleep
// wake recipe for BOOT (GPIO0), pad-hold set proven by the Rust firmware.
#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/rtc_io.h>
#include "soc/rtc_cntl_reg.h"

#include "battery.h"
#include "civil.h"
#include "config.h"
#include "console.h"
#include "DEV_Config.h"
#include "EPD_1in54g.h"
#include "framebuffer.h"
#include "net.h"
#include "ota.h"
#include "pcf85063.h"
#include "portal.h"
#include "screen.h"
#include "store.h"

// Power rails and buttons (schematic + reference/waveshare pin table).
#define PIN_BATT_ADC 4   // 200K/200K divider, VBAT = 2 x VADC
#define PIN_EPD_PWR 6    // EPD rail, active-low (LOW = on)
#define PIN_AUDIO_PWR 42 // audio rail, active-low (HIGH = off, unused)
#define PIN_VBAT_PATH 17 // battery latch, HIGH = powered; NEVER drive LOW
#define PIN_PWR_BTN 18   // idle LOW (board pulldown), pressed = VBAT via D2
#define PIN_BOOT_BTN 0   // idle HIGH, pressed = LOW; download mode at reset

#define WAKE_HOUR 6
#define CONSOLE_WINDOW_MS 15000UL

static uint8_t g_fb[FB_FRAME_LEN]; // 10KB: static, not stack

// Wake evidence: 1 = BOOT/ext0, 2 = PWR/ext1, 3 = other GPIO. Written only
// on real button wakes; RTC memory keeps the value across deep sleep and
// resets, power loss clears it. Read back via GET.
RTC_DATA_ATTR static uint8_t g_last_wake = 0;

static Record g_record;
static bool g_record_loaded = false;

// Console-set triggers for the current wake cycle.
static bool g_force_portal = false;
static bool g_force_ota = false;
// PWR long-press: park awake for flashing. Non-static: the panel driver's
// busy-wait (EPD_1in54g.cpp) adds to the hold accumulator too, so the
// gesture works during the refresh as well as in the console window.
bool g_stay_awake = false;
unsigned long g_pwr_hold_ms = 0; // cumulative PWR-pressed time this wake

// Wake forensics captured at boot (identify_wake), read back via GET.
// The boot-time diagnostic print races the host opening the serial port,
// so GET carries the values instead.
static int g_wake_cause = -1;
static uint32_t g_wake_raw = 0;
static uint32_t g_wake_ext1_fired = 0;
static int g_wake_reset = -1;
static int g_wake_boot_low = -1;

static bool load_record()
{
    static uint32_t buf[RECORD_LEN / 4]; // word-aligned: flash I/O requires it
    if (!ESP.flashRead(RECORD_SECTOR_ADDR, buf, RECORD_LEN)) {
        return false;
    }
    return record_unpack((const uint8_t *)buf, &g_record) == 1;
}

static void save_record()
{
    static uint32_t buf[RECORD_LEN / 4];
    record_pack(&g_record, (uint8_t *)buf);
    ESP.flashEraseSector(RECORD_SECTOR);
    if (!ESP.flashWrite(RECORD_SECTOR_ADDR, buf, RECORD_LEN)) {
        Serial.print("flash write failed\r\n");
    }
}

static int64_t seconds_until_next(int hour, const DateTime &now)
{
    const int64_t DAY = 24 * 3600;
    int64_t now_secs = (int64_t)now.hh * 3600 + now.mm * 60 + now.ss;
    int64_t target = (int64_t)hour * 3600;
    if (now_secs < target) {
        return target - now_secs;
    }
    return DAY - (now_secs - target);
}

// strtok-style whitespace tokenizer for the console grammar.
static char *next_tok(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    char *tok = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') s++;
    if (*s != '\0') *s++ = '\0';
    *p = s;
    return tok;
}

// Wake-source identification. Called early in setup() so the console window
// already reports THIS wake's evidence. The framework decoder
// (esp_sleep_get_wakeup_cause) reads EMPTY on this S3 for real button wakes
// (same silicon behaviour the Rust firmware hit; see README), so the
// decision falls back to raw registers + reset reason + the BOOT pin level:
// ext0 is level-triggered, so a BOOT wake means GPIO0 is still held LOW,
// while a timer wake finds it HIGH (pull-up).
static void identify_wake()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint32_t raw_cause = REG_GET_FIELD(RTC_CNTL_SLP_WAKEUP_CAUSE_REG, RTC_CNTL_WAKEUP_CAUSE);
    uint32_t ext1_fired = REG_GET_FIELD(RTC_CNTL_EXT_WAKEUP1_STATUS_REG, RTC_CNTL_EXT_WAKEUP1_STATUS);
    int rst = esp_reset_reason();
    int boot_low = (digitalRead(PIN_BOOT_BTN) == LOW);
    Serial.printf(
        "wake: cause=%d raw=0x%lx ext1_fired=0x%lx reset=%d boot_pin=%s\r\n",
        cause, (unsigned long)raw_cause, (unsigned long)ext1_fired, rst,
        boot_low ? "LOW" : "HIGH");

    bool pwr_wake = (cause == ESP_SLEEP_WAKEUP_EXT1) || (ext1_fired != 0);
    bool boot_wake = (cause == ESP_SLEEP_WAKEUP_EXT0) ||
                     (cause == ESP_SLEEP_WAKEUP_UNDEFINED && rst == ESP_RST_DEEPSLEEP &&
                      !pwr_wake && boot_low);
    if (pwr_wake) {
        g_last_wake = 2;
    } else if (boot_wake) {
        g_last_wake = 1;
    }
    g_wake_cause = (int)cause;
    g_wake_raw = raw_cause;
    g_wake_ext1_fired = ext1_fired;
    g_wake_reset = rst;
    g_wake_boot_low = boot_low;
}

// Final exit of every wake cycle: rails off, pad holds on, arm BOOT ext0 +
// timer, deep sleep (never returns). Vendor wake recipe from
// reference/vendor_power/power_manager.cc (xiaozhi-esp32, yunliao-s3 board).
static void enter_deep_sleep(int64_t secs)
{
    digitalWrite(EPD_LED_PIN, HIGH);   // LED off = sleeping
    digitalWrite(PIN_EPD_PWR, HIGH);   // EPD rail off
    digitalWrite(PIN_AUDIO_PWR, HIGH); // audio rail off
    digitalWrite(PIN_VBAT_PATH, HIGH); // battery latch stays on
    // Every pad is held at its driven level: deep sleep powers the digital
    // domain down, and a floating GPIO17 opens the battery latch = the board
    // never wakes again (Rust-firmware-verified pad set: GPIO3/6/17/42).
    gpio_hold_en((gpio_num_t)EPD_LED_PIN);
    gpio_hold_en(GPIO_NUM_6);
    gpio_hold_en(GPIO_NUM_17);
    gpio_hold_en(GPIO_NUM_42);
    gpio_deep_sleep_hold_en();

    // Wake sources: BOOT low level via ext0 (the vendor/factory recipe,
    // reference/vendor_power/power_manager.cc) + the timer.
    //
    // PWR (GPIO18, pressed = VBAT = HIGH) is deliberately NOT armed. Verified
    // 2026-09-25 on arduino-esp32 2.0.17 / IDF 4.4: esp_sleep_enable_ext1_
    // wakeup(ANY_HIGH) returns ESP_OK alongside ext0, but the ext1 hardware
    // never fires (arm result and sel/level snapshots read back via GET;
    // three real PWR presses produced no wake). The Rust firmware could arm
    // PWR alone on ext1 — under IDF the two buttons cannot combine (one
    // level per wake group, ext0 takes a single pin), so this port keeps the
    // factory semantics: BOOT wakes the device, PWR is the power-on button.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_en(GPIO_NUM_0);

    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    Serial.print("SLEEPING\r\n");
    Serial.flush();
    esp_deep_sleep_start();
}

// Streams the framebuffer already composed in g_fb to the panel:
// rail on -> init -> display -> panel sleep -> mandatory 2s settle ->
// rail off. Returns 0 ok / -1 busy timeout.
static int panel_show()
{
    digitalWrite(PIN_EPD_PWR, LOW); // active-low rail on, before panel reset
    int rc = EPD_1IN54G_Init();
    if (rc == 0) {
        rc = EPD_1IN54G_Display(g_fb);
    }
    EPD_1IN54G_Sleep();
    // Vendor close sequence: settle "at least 2s" after panel sleep before
    // anything cuts power, then RST low (DEV_Module_Exit with D_9PIN=0).
    digitalWrite(EPD_LED_PIN, HIGH); // panel work done: blink stops first
    delay(2000);
    DEV_Module_Exit();
    digitalWrite(PIN_EPD_PWR, HIGH); // EPD rail off
    return rc;
}

// One console command line, dispatched. Shared by the 15s console window
// AND the developer park loop — the park is a full-featured console, not a
// reduced one. Sleep/save/batttest outcomes flow back through the pointers.
static void console_dispatch(char *line, bool *save_needed,
                             int64_t *sleep_override, int *batttest_kind)
{
    char *p = line;
    char *cmd = next_tok(&p);
    if (cmd == NULL) return;

    if (strcmp(cmd, "SETDATE") == 0) {
        char *ds = next_tok(&p);
        char *ts = next_tok(&p);
        Date date;
        uint8_t hh, mm, ss;
        if (ds && ts && parse_date(ds, &date) && parse_hms(ts, &hh, &mm, &ss)) {
            DateTime dt;
            dt.date = date;
            dt.hh = hh;
            dt.mm = mm;
            dt.ss = ss;
            console_print(pcf85063_set_datetime(&dt) ? "OK DATE\r\n" : "ERR RTC\r\n");
        } else {
            console_print("ERR usage: SETDATE y-m-d h:m:s\r\n");
        }
    } else if (strcmp(cmd, "SETANNIV") == 0) {
        char *ds = next_tok(&p);
        Date date;
        if (ds && parse_date(ds, &date)) {
            g_record.anniv = date;
            *save_needed = true;
            console_print("OK ANNIV\r\n");
        } else {
            console_print("ERR usage: SETANNIV y-m-d\r\n");
        }
    } else if (strcmp(cmd, "SLEEP") == 0) {
        char *ss_ = next_tok(&p);
        if (ss_ && ss_[0] >= '0' && ss_[0] <= '9') {
            *sleep_override = atoll(ss_);
            console_print("OK SLEEP\r\n");
        } else {
            console_print("ERR usage: SLEEP seconds\r\n");
        }
    } else if (strcmp(cmd, "BATTTEST") == 0) {
        char *what = next_tok(&p);
        if (what && strcmp(what, "LOW") == 0) {
            *batttest_kind = 1;
            console_print("OK BATTTEST LOW\r\n");
        } else if (what && strcmp(what, "CRIT") == 0) {
            *batttest_kind = 2;
            console_print("OK BATTTEST CRIT\r\n");
        } else {
            console_print("ERR usage: BATTTEST LOW|CRIT\r\n");
        }
    } else if (strcmp(cmd, "BTN") == 0) {
        // both buttons are active-LOW (schematic); report pressed/resting
        Serial.printf("PWR(GPIO18) %s\r\nBOOT(GPIO0) %s\r\n",
                      digitalRead(PIN_PWR_BTN) == LOW ? "PRESSED" : "up",
                      digitalRead(PIN_BOOT_BTN) == LOW ? "PRESSED" : "up");
    } else if (strcmp(cmd, "GET") == 0) {
        DateTime rtc_now;
        int osc_stopped = 0;
        if (pcf85063_read_datetime(&rtc_now, &osc_stopped)) {
            // battery live: re-read so park-session GETs track reality
            uint32_t mv = (uint32_t)analogReadMilliVolts(PIN_BATT_ADC) * 2;
            if (mv > 5000) mv = 5000;
            Serial.printf(
                "FW %s ANNIV %04d-%02d-%02d LAST %lld VBAT %umV KIND %u "
                "RTC_STOPPED %d WAKE %u\r\n",
                FW_VERSION,
                g_record.anniv.y, g_record.anniv.m, g_record.anniv.d,
                (long long)g_record.last_shown_days, mv,
                g_record.screen_kind, osc_stopped, g_last_wake);
            Serial.printf(
                "wake_ev: cause=%d raw=0x%lx ext1_fired=0x%lx reset=%d "
                "boot_pin=%s\r\n",
                g_wake_cause, (unsigned long)g_wake_raw,
                (unsigned long)g_wake_ext1_fired, g_wake_reset,
                g_wake_boot_low ? "LOW" : "HIGH");
        }
    } else if (strcmp(cmd, "WIFI") == 0) {
        Serial.printf("SSID %s\r\nNTP %s\r\nOTA %s\r\n",
                      cfg::wifi_ssid().c_str(), cfg::ntp_host().c_str(),
                      cfg::ota_url().length() ? cfg::ota_url().c_str() : "(disabled)");
    } else if (strcmp(cmd, "SETWIFI") == 0) {
        char *s = next_tok(&p);
        char *w = next_tok(&p);
        if (s && w) {
            cfg::wifi_save(String(s), String(w));
            console_print("OK WIFI\r\n");
        } else {
            console_print("ERR usage: SETWIFI ssid password\r\n");
        }
    } else if (strcmp(cmd, "FORGETWIFI") == 0) {
        cfg::wifi_forget();
        console_print("OK FORGOT\r\n");
    } else if (strcmp(cmd, "SETNTP") == 0) {
        char *s = next_tok(&p);
        if (s) {
            cfg::set_ntp_host(String(s));
            console_print("OK NTP\r\n");
        } else {
            console_print("ERR usage: SETNTP host\r\n");
        }
    } else if (strcmp(cmd, "SETOTAURL") == 0) {
        char *s = next_tok(&p);
        if (s) {
            cfg::set_ota_url(String(s));
            console_print("OK OTA\r\n");
        } else {
            console_print("ERR usage: SETOTAURL url\r\n");
        }
    } else if (strcmp(cmd, "FORCEOTA") == 0) {
        g_force_ota = true;
        console_print("OK FORCEOTA\r\n");
    } else {
        console_print("ERR?\r\n");
    }
}

void setup()
{
    // Battery latch FIRST: on a PWR-button cold boot the button is the only
    // thing feeding the board until GPIO17 goes high. (Never drive LOW.)
    pinMode(PIN_VBAT_PATH, OUTPUT);
    digitalWrite(PIN_VBAT_PATH, HIGH);

    // Release pad holds from deep sleep before reconfiguring the pads.
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)EPD_LED_PIN); // GPIO3
    gpio_hold_dis(GPIO_NUM_6);
    gpio_hold_dis(GPIO_NUM_17);
    gpio_hold_dis(GPIO_NUM_42);

    DEV_Module_Init(); // vendor pin init (rail untouched: D_9PIN=0)
    pinMode(EPD_LED_PIN, OUTPUT);
    digitalWrite(EPD_LED_PIN, HIGH); // LED off (low-active)
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH); // EPD rail off
    pinMode(PIN_AUDIO_PWR, OUTPUT);
    digitalWrite(PIN_AUDIO_PWR, HIGH); // audio rail off
    pinMode(PIN_PWR_BTN, INPUT_PULLUP); // BAT_KEY is ACTIVE-LOW: 10K pull-up
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP); // to 3V3 on the board (schematic R58),
    // pressed = pulled to GND through D8. The legacy brief said "pulldown,
    // pressed = HIGH" — that description does not match the real board and
    // made every PWR press invisible to the firmware.

    // identify the wake source before anything else prints (see comment there)
    identify_wake();
    cfg::begin(); // NVS-backed settings (WiFi creds, NTP/OTA endpoints)

    g_record_loaded = load_record();
    if (!g_record_loaded) {
        Serial.print("flash record blank, using placeholder\r\n");
        record_placeholder(&g_record);
    }

    // Battery voltage: VBAT via 200K/200K divider on GPIO4 (ADC1_CH3).
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
    uint32_t vbat_mv = (uint32_t)analogReadMilliVolts(PIN_BATT_ADC) * 2;
    if (vbat_mv > 5000) vbat_mv = 5000;

    Wire.begin(47, 48); // PCF85063 on I2C0 (SDA=47 SCL=48, official pin table)
    Wire.setClock(100000);

    // ---- console phase (USB-CDC / USB-Serial-JTAG), max ~15s ----
    console_print("molly-count console\r\n"
                  "SETDATE y-m-d h:m:s | SETANNIV y-m-d | GET | SLEEP s | "
                  "BATTTEST LOW|CRIT\r\n");
    unsigned long console_start = millis();
    int last_pwr = digitalRead(PIN_PWR_BTN);
    int last_boot = digitalRead(PIN_BOOT_BTN);
    // Button gesture bookkeeping. Polarity differs: PWR pressed = HIGH,
    // BOOT pressed = LOW. A button already held at window start (e.g. BOOT:
    // the wake press itself) counts as held-since-now — the natural "wake
    // and keep holding" gesture has no press edge inside the window.
    bool boot_was_pressed = (last_boot == LOW);
    unsigned long boot_down = boot_was_pressed ? millis() : 0;
    unsigned long last_iter_ms = millis();
    Serial.printf("btn idle: PWR(GPIO18)=%s BOOT(GPIO0)=%s\r\n",
                  last_pwr ? "HIGH" : "LOW", last_boot ? "HIGH" : "LOW");
    bool save_needed = false;
    int64_t sleep_override = -1;
    int batttest_kind = -1; // forced battery kind for screen demos
    char line[64];
    unsigned long window_deadline = console_start + CONSOLE_WINDOW_MS;
    while (millis() < window_deadline && !g_force_portal) {
        // slow heartbeat while the console window is open: the sealed-case
        // "awake and waiting" signal (fast blink = panel busy, off = asleep).
        // Once the park gesture is armed the LED goes solid immediately as
        // its feedback instead of continuing the blink.
        if (!g_stay_awake) {
            digitalWrite(EPD_LED_PIN, !digitalRead(EPD_LED_PIN));
        }

        int pwr_now = digitalRead(PIN_PWR_BTN);
        int boot_now = digitalRead(PIN_BOOT_BTN);
        unsigned long now_ms = millis();

        // button gestures (held >=5s during the window): BOOT long-press =
        // re-open provisioning; PWR = park awake for flashing, CUMULATIVE —
        // split presses count toward the same 5s (single-hold-only proved
        // hostile in bench testing).
        bool pwr_pressed = (pwr_now == LOW);  // active-LOW button
        bool boot_pressed = (boot_now == LOW);
        // Holding any button keeps the window open — extend only, never
        // shorten (an early extension used to TRUNCATE the 15s window to
        // ~7s when BOOT was held from the wake: now+6 < start+15)
        if (pwr_pressed || boot_pressed) {
            unsigned long ext = millis() + 6000;
            if (ext > window_deadline) window_deadline = ext;
        }
        if (pwr_pressed) g_pwr_hold_ms += now_ms - last_iter_ms;
        last_iter_ms = now_ms;
        if (boot_pressed && !boot_was_pressed) boot_down = millis();
        if (!boot_pressed && boot_was_pressed) boot_down = 0;
        boot_was_pressed = boot_pressed;
        if (!g_force_portal && boot_down && millis() - boot_down > 5000) {
            g_force_portal = true;
            Serial.print("portal: re-provision gesture, opening now\r\n");
            // break out of the window immediately: the portal (and the
            // setup screen) is the feedback, no waiting for the slow blink
        }
        if (!g_stay_awake && g_pwr_hold_ms >= 5000) {
            g_stay_awake = true;
            digitalWrite(EPD_LED_PIN, LOW); // solid on right away: armed
            Serial.print("stay: park armed (PWR 5s cumulative)\r\n");
        }

        if (pwr_now != last_pwr || boot_now != last_boot) {
            Serial.printf("btn edge: PWR(GPIO18)=%s BOOT(GPIO0)=%s\r\n",
                          pwr_now ? "HIGH" : "LOW", boot_now ? "HIGH" : "LOW");
            last_pwr = pwr_now;
            last_boot = boot_now;
        }

        if (!console_read_line(600, line, sizeof line)) continue;
        console_dispatch(line, &save_needed, &sleep_override, &batttest_kind);
    }
    if (save_needed) {
        save_record();
        console_print("SAVED\r\n");
    }

    // ---- provisioning portal ----
    // First wake with no stored credentials, or the long-press gesture:
    // show the setup screen and wait up to 5 minutes for the phone.
    bool want_portal = g_force_portal || (!cfg::wifi_saved() && !cfg::portal_seen());
    if (want_portal) {
        Serial.print("portal: opening (AP molly-count-Setup)\r\n");
        screen_draw_setup(g_fb);
        panel_show();
        if (portal_run(5UL * 60000UL)) {
            Serial.print("portal: credentials saved\r\n");
        }
        cfg::mark_portal_seen();
        g_force_portal = false;
    }

    // ---- network cycle: WiFi + NTP (resyncs the RTC) + OTA check ----
    // Radio goes back off inside net_wake_cycle unless we keep it for OTA.
    NetResult net = net_wake_cycle(true);
    if (net.wifi_ok) {
        ota_check_and_apply(vbat_mv, g_force_ota); // reboots the device if applied
        net_radio_off();
    }
    g_force_ota = false;

    // ---- day + battery state ----
    DateTime now;
    int osc_stopped = 0;
    if (!pcf85063_read_datetime(&now, &osc_stopped)) {
        Serial.print("ERR RTC READ - sleeping 1h to avoid a refresh loop\r\n");
        enter_deep_sleep(3600);
    }
    // Implausible RTC date (power loss without NTP rescue): show the last
    // good count instead of computing nonsense from a 2000-year date.
    bool rtc_ok = (now.date.y >= 2024);
    int64_t days;
    if (rtc_ok) {
        days = date_day_number(g_record.anniv, now.date); // anniversary = day 1
    } else {
        Serial.print("ERR RTC implausible - showing last count\r\n");
        days = (g_record.last_shown_days == RECORD_NEVER_SHOWN)
                   ? 1
                   : g_record.last_shown_days;
    }
    BattState prev_state = batt_from_kind_byte(g_record.screen_kind);
    BattState state = (batttest_kind >= 0) ? batt_from_kind_byte((uint8_t)batttest_kind)
                                           : battery_next_state(prev_state, vbat_mv);
    uint8_t kind = batt_kind_byte(state);
    Serial.printf(
        "rtc: %04d-%02d-%02d %02d:%02d:%02d, days: %lld, vbat: %umV, batt: %u, "
        "last: %lld, kind: %u, wake_ev: %u, wifi: %d\r\n",
        now.date.y, now.date.m, now.date.d, now.hh, now.mm, now.ss,
        (long long)days, vbat_mv, (unsigned)state,
        (long long)g_record.last_shown_days, g_record.screen_kind, g_last_wake,
        net.wifi_ok);

    // ---- full refresh (~20s) ----
    // Every wake refreshes once before going back down (migration spec):
    // the visible flicker is the proof of life, and the day/battery state
    // is redrawn whatever changed.
    Serial.print("screen update -> full refresh\r\n");
    unsigned long t0 = millis();
    screen_draw(g_fb, state, g_record.anniv, days, now.date, net.wifi_ok);
    int rc = panel_show();
    Serial.printf("refresh rc=%d done in %lu ms\r\n", rc, millis() - t0);

    // BATTTEST screens are demos: don't persist them as real state. Also
    // skip persisting when the refresh timed out (panel may not have shown).
    if (batttest_kind < 0 && rc == 0) {
        g_record.last_shown_days = days;
        g_record.screen_kind = kind;
        save_record();
    }
    console_print("DISPLAY UPDATED\r\n");
    ota_mark_boot_ok(); // one full successful cycle: promote a pending OTA

    // ---- developer park (PWR long-press) ----
    // Stay awake with the serial port up so `pio run -t upload` can flash
    // at any time, no BOOT press or watcher needed. Solid LED = parked
    // (reachable only deliberately; unplanned solid light is still the
    // hung-firmware alarm). Exits: any button tap, console SLEEP, timeout.
    if (g_stay_awake) {
        const unsigned long PARK_MS = 30UL * 60000UL;
        Serial.print("stay: parked awake, port up (max 30 min; any button exits)\r\n");
        console_print("PARKED\r\n");
        digitalWrite(EPD_LED_PIN, LOW); // solid on = parked
        unsigned long start = millis();
        unsigned long last_heartbeat = 0;
        while (millis() - start < PARK_MS) {
            // any button tap exits the park (both buttons are active-LOW)
            if (digitalRead(PIN_PWR_BTN) == LOW || digitalRead(PIN_BOOT_BTN) == LOW) {
                Serial.print("stay: button pressed, exiting park\r\n");
                break;
            }
            // heartbeat pauses while a line is being processed; slow blink
            // resumes otherwise (solid here would read as "hung")
            char line[64];
            if (console_read_line(400, line, sizeof line)) {
                bool save = false;
                int64_t so = -1;
                int bt = -1;
                console_dispatch(line, &save, &so, &bt);
                if (so >= 0) {
                    // SLEEP from the park: leave now, sleep override applies
                    Serial.print("stay: SLEEP, exiting park\r\n");
                    if (save) save_record();
                    break;
                }
                if (save) save_record();
                start = millis(); // keep the park alive while the dev is active
            } else if (millis() - last_heartbeat > 500) {
                digitalWrite(EPD_LED_PIN, !digitalRead(EPD_LED_PIN));
                last_heartbeat = millis();
            }
        }
        digitalWrite(EPD_LED_PIN, HIGH); // off again before sleeping
        g_stay_awake = false;
        g_pwr_hold_ms = 0; // next wake starts the gesture count fresh
    }

    int64_t secs = (sleep_override >= 0) ? sleep_override
                                         : seconds_until_next(WAKE_HOUR, now);
    Serial.printf("deep sleep for %lld s (wake at %02d:00)\r\n",
                  (long long)secs, WAKE_HOUR);
    enter_deep_sleep(secs);
}

void loop()
{
    // unreachable: setup() always ends in esp_deep_sleep_start()
}
