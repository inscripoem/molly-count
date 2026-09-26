#include "config.h"

#include <Preferences.h>

namespace {

Preferences prefs;

const char *KEY_SSID = "ssid";
const char *KEY_PASS = "pass";
const char *KEY_NTP = "ntp";
const char *KEY_OTA = "ota";
const char *KEY_PORTAL = "portal";

const char *DEF_NTP = "ntp.aliyun.com";

} // namespace

namespace cfg {

void begin()
{
    prefs.begin("molly", false);
}

bool wifi_saved()
{
    return prefs.getString(KEY_SSID, "").length() > 0;
}

String wifi_ssid() { return prefs.getString(KEY_SSID, ""); }
String wifi_pass() { return prefs.getString(KEY_PASS, ""); }

void wifi_save(const String &ssid, const String &pass)
{
    prefs.putString(KEY_SSID, ssid);
    prefs.putString(KEY_PASS, pass);
}

void wifi_forget()
{
    prefs.remove(KEY_SSID);
    prefs.remove(KEY_PASS);
}

String ntp_host() { return prefs.getString(KEY_NTP, DEF_NTP); }
void set_ntp_host(const String &h) { prefs.putString(KEY_NTP, h); }

String ota_url() { return prefs.getString(KEY_OTA, ""); }
void set_ota_url(const String &u) { prefs.putString(KEY_OTA, u); }

bool portal_seen() { return prefs.getBool(KEY_PORTAL, false); }
void mark_portal_seen() { prefs.putBool(KEY_PORTAL, true); }

} // namespace cfg
