// molly-count — persistent settings in NVS (flash-backed: survives power
// loss, deep sleep, re-flashing; erased only by erase_flash). Arduino
// Preferences wrapper, namespace "molly".
#pragma once

#include <Arduino.h>

namespace cfg {

void begin();

bool wifi_saved();
String wifi_ssid();
String wifi_pass();
void wifi_save(const String &ssid, const String &pass);
void wifi_forget();

String ntp_host();                    // default "ntp.aliyun.com"
void set_ntp_host(const String &h);

String ota_url();                     // default "" = OTA disabled
void set_ota_url(const String &u);

bool portal_seen();                   // auto-provision-once bookkeeping
void mark_portal_seen();

} // namespace cfg
