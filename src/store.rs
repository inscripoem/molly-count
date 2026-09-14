//! Persistent settings record, packed into one dedicated 4KB flash sector
//! inside the factory NVS partition region (nothing else uses NVS in v1).
//!
//! Pure pack/unpack + checksum logic, unit-tested on the host; the flash I/O
//! glue lives in the bin.

use crate::civil::Date;

/// 0xC000: third sector of the nvs partition (0x9000..0xF000).
pub const SECTOR: u32 = 0xC000;
pub const RECORD_LEN: usize = 32;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Record {
    pub anniv: Date,
    /// Day count currently shown on the panel; drives the "skip if unchanged"
    /// optimization. `i64::MIN` means "never shown".
    pub last_shown_days: i64,
    /// Battery/screen kind currently on the panel (`battery::BattState`
    /// kind byte: 0 normal, 1 low, 2 charge-warning). Written at offset 24,
    /// OUTSIDE the checksummed region, so records saved by firmware before
    /// this field existed remain valid (offset 24 reads 0xFF there and
    /// decodes as Normal, matching what the panel really showed).
    pub screen_kind: u8,
}

impl Record {
    pub const NEVER_SHOWN: i64 = i64::MIN;

    pub const fn placeholder() -> Self {
        Self {
            anniv: Date::new(2025, 2, 14),
            last_shown_days: Self::NEVER_SHOWN,
            screen_kind: 0,
        }
    }

    pub fn pack(&self) -> [u8; RECORD_LEN] {
        let mut b = [0u8; RECORD_LEN];
        b[0..4].copy_from_slice(b"MCV1");
        b[4..8].copy_from_slice(&self.anniv.y.to_le_bytes());
        b[8] = self.anniv.m;
        b[9] = self.anniv.d;
        b[10..18].copy_from_slice(&self.last_shown_days.to_le_bytes());
        let sum = fnv1a(&b[0..20]);
        b[20..24].copy_from_slice(&sum.to_le_bytes());
        b[24] = self.screen_kind;
        b
    }

    pub fn unpack(b: &[u8; RECORD_LEN]) -> Option<Self> {
        if b[0..4] != *b"MCV1" {
            return None;
        }
        let sum = u32::from_le_bytes([b[20], b[21], b[22], b[23]]);
        if sum != fnv1a(&b[0..20]) {
            return None;
        }
        let y = i32::from_le_bytes([b[4], b[5], b[6], b[7]]);
        let m = b[8];
        let d = b[9];
        if !(1..=12).contains(&m) || !(1..=31).contains(&d) {
            return None;
        }
        Some(Self {
            anniv: Date::new(y, m, d),
            last_shown_days: i64::from_le_bytes([
                b[10], b[11], b[12], b[13], b[14], b[15], b[16], b[17],
            ]),
            screen_kind: b[24],
        })
    }
}

/// 32-bit FNV-1a.
fn fnv1a(data: &[u8]) -> u32 {
    let mut h: u32 = 0x811C_9DC5;
    for &b in data {
        h ^= b as u32;
        h = h.wrapping_mul(0x0100_0193);
    }
    h
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        let r = Record {
            anniv: Date::new(2025, 2, 14),
            last_shown_days: 577,
            screen_kind: 0,
        };
        let unpacked = Record::unpack(&r.pack()).unwrap();
        assert_eq!(unpacked, r);
    }

    #[test]
    fn never_shown_sentinel_roundtrips() {
        let r = Record::placeholder();
        let unpacked = Record::unpack(&r.pack()).unwrap();
        assert_eq!(unpacked.last_shown_days, i64::MIN);
    }

    #[test]
    fn bad_magic_rejected() {
        let mut b = Record::placeholder().pack();
        b[0] = b'X';
        assert!(Record::unpack(&b).is_none());
    }

    #[test]
    fn corrupted_payload_rejected() {
        let mut b = Record::placeholder().pack();
        b[8] = 13; // invalid month, checksum now stale
        assert!(Record::unpack(&b).is_none());
    }

    #[test]
    fn legacy_record_without_flags_still_unpacks() {
        // bytes written by pre-battery firmware: 0..24 valid, 24.. = 0xFF
        let mut r = Record {
            anniv: Date::new(2024, 6, 1),
            last_shown_days: 835,
            screen_kind: 0xFF,
        };
        let mut b = {
            r.screen_kind = 0;
            let mut b = r.pack();
            b[24] = 0xFF; // emulate the old flash content
            b
        };
        let u = Record::unpack(&b).unwrap();
        assert_eq!(u.anniv, Date::new(2024, 6, 1));
        assert_eq!(u.last_shown_days, 835);
        assert_eq!(u.screen_kind, 0xFF); // caller (battery.rs) maps 0xFF -> Normal
        b[24] = 1; // low-battery screen shown
        assert_eq!(Record::unpack(&b).unwrap().screen_kind, 1);
    }

    #[test]
    fn flags_roundtrip() {
        let mut r = Record::placeholder();
        r.screen_kind = 2;
        assert_eq!(Record::unpack(&r.pack()).unwrap().screen_kind, 2);
    }

    #[test]
    fn out_of_range_date_rejected() {
        let mut r = Record::placeholder().pack();
        r[8] = 0; // month 0 — checksum-consistent garbage
        // recompute checksum so only the range check can reject it
        let mut h: u32 = 0x811C_9DC5;
        for &b in &r[0..20] {
            h ^= b as u32;
            h = h.wrapping_mul(0x0100_0193);
        }
        r[20..24].copy_from_slice(&h.to_le_bytes());
        assert!(Record::unpack(&r).is_none());
    }
}
