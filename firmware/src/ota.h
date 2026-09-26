// molly-count — OTA client. Pulls <ota_url>fw.version, compares against
// FW_VERSION, and if newer (or forced) applies <ota_url>molly.bin via
// esp_https_ota (TLS with the ESP certificate bundle — works with any
// major-CA host, e.g. Aliyun OSS or GitHub Releases).
//
// Gates: battery must be >= OTA_MIN_BATTERY_MV unless forced; a failed
// apply leaves the running firmware untouched. The new image boots in
// pending-verify state (bootloader rollback is enabled) and the running
// firmware marks it valid only after one fully successful wake cycle.
#pragma once

#include <Arduino.h>

#define OTA_MIN_BATTERY_MV 3600

struct OtaOutcome {
    bool reached;           // server contacted at all
    bool update_available;  // a newer version exists
    bool applied;           // image written; caller must esp_restart()
    int err;                // last esp_err_t seen (0 = none)
};

OtaOutcome ota_check_and_apply(uint16_t battery_mv, bool force);

// After one fully successful wake cycle, promote a pending-verify OTA
// image to valid (no-op when the running image is not pending).
void ota_mark_boot_ok();
