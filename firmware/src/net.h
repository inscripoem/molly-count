// molly-count — WiFi connect + NTP resync of the PCF85063.
//
// Design contract: the network must never hold the device hostage. Every
// step has a hard timeout and every failure falls back to the RTC-only
// behavior (the legacy firmware flow). Radio is switched off before return.
#pragma once

struct NetResult {
    bool wifi_ok;
    bool ntp_ok;
};

// Connect with stored credentials (if any), sync NTP into the PCF85063.
// Total worst case ~18s inside the timeouts. Pass keep_radio=true to leave
// the station up (for OTA); the caller must then call net_radio_off().
NetResult net_wake_cycle(bool keep_radio);

// Unconditionally bring the radio down (STA off + esp_wifi_stop).
void net_radio_off();
