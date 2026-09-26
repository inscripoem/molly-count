// Ported from src/store.rs #[cfg(test)] — same cases; the packed layout must
// stay byte-compatible with the legacy Rust firmware.
#include <string.h>

#include "store.h"
#include "test_harness.h"

static Record R(int y, int m, int d, int64_t days, uint8_t kind)
{
    Record r;
    r.anniv.y = y;
    r.anniv.m = (uint8_t)m;
    r.anniv.d = (uint8_t)d;
    r.last_shown_days = days;
    r.screen_kind = kind;
    return r;
}

void run_store_tests()
{
    // roundtrip
    {
        Record r = R(2025, 2, 14, 577, 0);
        uint8_t b[RECORD_LEN];
        record_pack(&r, b);
        Record u;
        CHECK(record_unpack(b, &u) == 1);
        CHECK(u.anniv.y == 2025 && u.anniv.m == 2 && u.anniv.d == 14);
        CHECK(u.last_shown_days == 577);
        CHECK(u.screen_kind == 0);
    }

    // never_shown_sentinel_roundtrips
    {
        Record p;
        record_placeholder(&p);
        uint8_t b[RECORD_LEN];
        record_pack(&p, b);
        Record u;
        CHECK(record_unpack(b, &u) == 1);
        CHECK(u.last_shown_days == RECORD_NEVER_SHOWN);
    }

    // bad_magic_rejected
    {
        Record p;
        record_placeholder(&p);
        uint8_t b[RECORD_LEN];
        record_pack(&p, b);
        b[0] = 'X';
        Record u;
        CHECK(record_unpack(b, &u) == 0);
    }

    // corrupted_payload_rejected
    {
        Record p;
        record_placeholder(&p);
        uint8_t b[RECORD_LEN];
        record_pack(&p, b);
        b[8] = 13; // invalid month, checksum now stale
        Record u;
        CHECK(record_unpack(b, &u) == 0);
    }

    // legacy_record_without_flags_still_unpacks (bytes written by
    // pre-battery firmware: 0..24 valid, 24.. = 0xFF)
    {
        Record r = R(2024, 6, 1, 835, 0);
        uint8_t b[RECORD_LEN];
        record_pack(&r, b);
        b[24] = 0xFF; // emulate the old flash content
        Record u;
        CHECK(record_unpack(b, &u) == 1);
        CHECK(u.anniv.y == 2024 && u.anniv.m == 6 && u.anniv.d == 1);
        CHECK(u.last_shown_days == 835);
        CHECK(u.screen_kind == 0xFF); // caller maps 0xFF -> Normal
        b[24] = 1;
        CHECK(record_unpack(b, &u) == 1 && u.screen_kind == 1);
    }

    // flags_roundtrip
    {
        Record r = R(2025, 2, 14, RECORD_NEVER_SHOWN, 2);
        uint8_t b[RECORD_LEN];
        record_pack(&r, b);
        Record u;
        CHECK(record_unpack(b, &u) == 1 && u.screen_kind == 2);
    }

    // out_of_range_date_rejected (month 0 with a checksum-consistent pack)
    {
        Record r = R(2025, 0, 10, 42, 0);
        uint8_t b[RECORD_LEN];
        record_pack(&r, b);
        Record u;
        CHECK(record_unpack(b, &u) == 0);
    }

    // cross-firmware byte compatibility: the exact pack output for a known
    // record must match the Rust implementation byte for byte
    {
        Record r = R(2025, 2, 14, 577, 0);
        uint8_t b[RECORD_LEN];
        record_pack(&r, b);
        static const uint8_t expect[RECORD_LEN] = {
            'M',  'C',  'V',  '1',  // magic
            0xE9, 0x07, 0x00, 0x00, // 2025 LE
            0x02, 0x0E,             // month, day
            0x41, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 577 LE
            // FNV1a(b[0..20]) LE — computed below by reference impl
        };
        CHECK(memcmp(b, expect, 18) == 0);
        // independent FNV1a recomputation over b[0..20]
        uint32_t h = 0x811C9DC5u;
        for (int i = 0; i < 20; i++) {
            h ^= (uint32_t)b[i];
            h *= 0x01000193u;
        }
        CHECK(b[20] == (uint8_t)(h & 0xFF) && b[21] == (uint8_t)((h >> 8) & 0xFF) &&
              b[22] == (uint8_t)((h >> 16) & 0xFF) && b[23] == (uint8_t)((h >> 24) & 0xFF));
        CHECK(b[24] == 0 && b[25] == 0);
    }
}
