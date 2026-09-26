#include "ota.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <WiFi.h>

#include "config.h"
#include "version.h"

namespace {

// Fetch <base>fw.version into out (NUL-terminated). false on any error.
bool fetch_version_text(const String &base, char *out, size_t outlen)
{
    String url = base + "fw.version";
    esp_http_client_config_t c = {};
    c.url = url.c_str();
    c.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    c.timeout_ms = 10000;

    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (h == nullptr) return false;

    bool ok = false;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        int len = esp_http_client_fetch_headers(h);
        int status = esp_http_client_get_status_code(h);
        if (status == 200 && len > 0) {
            int n = esp_http_client_read(h, out, outlen - 1);
            if (n > 0) {
                out[n] = '\0';
                // trim trailing whitespace/newlines
                while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                                 out[n - 1] == ' ')) {
                    out[--n] = '\0';
                }
                ok = true;
            }
        }
        esp_http_client_close(h);
    } else {
        esp_http_client_cleanup(h);
    }
    return ok;
}

} // namespace

OtaOutcome ota_check_and_apply(uint16_t battery_mv, bool force)
{
    OtaOutcome r{false, false, false, 0};

    String base = cfg::ota_url();
    if (base.length() == 0) return r; // OTA not configured
    if (base[base.length() - 1] != '/') base += '/';
    if (!force && battery_mv < OTA_MIN_BATTERY_MV) {
        Serial.printf("ota: battery %umV below gate, skip\r\n", battery_mv);
        return r;
    }

    char ver[24] = {0};
    if (!fetch_version_text(base, ver, sizeof ver)) {
        r.err = -1;
        Serial.print("ota: version check unreachable\r\n");
        return r;
    }
    r.reached = true;

    if (!force && fw_ver_cmp(ver, FW_VERSION) <= 0) {
        Serial.printf("ota: up to date (running " FW_VERSION ", remote %s)\r\n", ver);
        return r;
    }
    r.update_available = true;
    Serial.printf("ota: update " FW_VERSION " -> %s\r\n", ver);

    String bin = base + "molly.bin";
    esp_http_client_config_t http = {};
    http.url = bin.c_str();
    http.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    http.timeout_ms = 30000;
    http.buffer_size = 4096;

    // IDF 4.4's simple variant: takes the http config directly (the
    // esp_https_ota_config_t struct form arrived in IDF 5).
    esp_err_t e = esp_https_ota(&http);
    r.err = (int)e;
    if (e == ESP_OK) {
        r.applied = true;
        Serial.print("ota: applied, rebooting (pending-verify)\r\n");
        delay(500);
        ESP.restart();
    } else {
        Serial.printf("ota: apply failed (%d)\r\n", e);
    }
    return r;
}

// Called after one fully successful wake cycle (screen refreshed, record
// saved): promotes a pending-verify OTA image to valid. No-op otherwise.
void ota_mark_boot_ok()
{
    esp_ota_mark_app_valid_cancel_rollback();
}
