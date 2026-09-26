#include "net.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

#include "civil.h"
#include "config.h"
#include "DEV_Config.h"
#include "pcf85063.h"

namespace {

// Hard cap on the whole station connect: WL_CONNECTED or give up.
bool wifi_connect_sta(unsigned long timeout_ms)
{
    String ssid = cfg::wifi_ssid();
    String pass = cfg::wifi_pass();
    if (ssid.length() == 0) return false;

    WiFi.persistent(false); // we manage credentials in our own NVS namespace
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // slow blink while connecting: part of the "awake" LED language
    unsigned long start = millis();
    unsigned long last_blink = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) return false;
        if (millis() - last_blink > 300) {
            digitalWrite(EPD_LED_PIN, !digitalRead(EPD_LED_PIN));
            last_blink = millis();
        }
        delay(100);
    }
    return true;
}

void radio_off()
{
    WiFi.disconnect(false, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
}

} // namespace

void net_radio_off()
{
    radio_off();
}

NetResult net_wake_cycle(bool keep_radio)
{
    NetResult r{false, false};

    if (!wifi_connect_sta(10000)) {
        radio_off();
        return r;
    }
    r.wifi_ok = true;

    // NTP: wait until SNTP sets the system clock (UTC+8), then write the
    // truth into the PCF85063 so the RTC stays the single source of time
    // and its drift is healed at the source. Manual wait loop (instead of
    // getLocalTime) so the LED keeps its slow blink while waiting.
    configTime(8 * 3600, 0, cfg::ntp_host().c_str());
    bool ntp_set = false;
    struct tm t;
    time_t utc = 0;
    unsigned long start = millis();
    unsigned long last_blink = 0;
    while (millis() - start < 8000) {
        utc = time(nullptr);
        if (utc > 1700000000) { // SNTP has set a plausible clock
            ntp_set = true;
            break;
        }
        if (millis() - last_blink > 300) {
            digitalWrite(EPD_LED_PIN, !digitalRead(EPD_LED_PIN));
            last_blink = millis();
        }
        delay(100);
    }
    if (ntp_set) {
        localtime_r(&utc, &t);
        DateTime dt;
        dt.date.y = t.tm_year + 1900;
        dt.date.m = (uint8_t)(t.tm_mon + 1);
        dt.date.d = (uint8_t)t.tm_mday;
        dt.hh = (uint8_t)t.tm_hour;
        dt.mm = (uint8_t)t.tm_min;
        dt.ss = (uint8_t)t.tm_sec;
        r.ntp_ok = (pcf85063_set_datetime(&dt) == 1);
    }

    if (!keep_radio) {
        radio_off();
    }
    return r;
}
