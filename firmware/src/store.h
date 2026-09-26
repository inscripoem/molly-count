// molly-count — persistent settings record, packed into one dedicated 4KB
// flash sector. Ported from src/store.rs; the packed layout is BYTE-COMPATIBLE
// with the legacy Rust firmware ("MCV1" magic + FNV1a checksum). Pure
// pack/unpack + checksum logic, host-testable; flash I/O lives in main.cpp.
#pragma once

#include <stdint.h>

#include "civil.h"

// 0xC000: kept OUTSIDE every partition of firmware/partitions_molly.csv so
// re-flashing never touches it (the Rust firmware wrote it inside the
// factory NVS region, but nothing else ever used NVS).
#define RECORD_SECTOR_ADDR 0xC000u
#define RECORD_SECTOR (RECORD_SECTOR_ADDR / 4096u) // esptool/flash sector index
#define RECORD_LEN 32

typedef struct {
    Date anniv;
    // Day count currently shown on the panel; drives nothing anymore (every
    // wake refreshes) but kept byte-compatible. NEVER_SHOWN = never shown.
    int64_t last_shown_days;
    // Battery/screen kind currently on the panel (battery.h kind byte).
    // Lives at offset 24, OUTSIDE the checksummed region, so records saved
    // by firmware before this field existed remain valid (offset 24 reads
    // 0xFF there and decodes as Normal).
    uint8_t screen_kind;
} Record;

#define RECORD_NEVER_SHOWN ((int64_t)0x8000000000000000LL) // i64::MIN

void record_placeholder(Record *r);

void record_pack(const Record *r, uint8_t out[RECORD_LEN]);

// 1 ok / 0 rejected (bad magic, bad checksum, out-of-range date).
int record_unpack(const uint8_t b[RECORD_LEN], Record *out);
