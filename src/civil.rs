//! Proleptic Gregorian calendar math (Howard Hinnant's `days_from_civil`).
//!
//! Pure `core` logic, unit-tested on the host. Day 0 = 1970-01-01.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Date {
    pub y: i32,
    pub m: u8,
    pub d: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DateTime {
    pub date: Date,
    pub hh: u8,
    pub mm: u8,
    pub ss: u8,
}

impl DateTime {
    pub const fn new(date: Date, hh: u8, mm: u8, ss: u8) -> Self {
        Self { date, hh, mm, ss }
    }
}

impl Date {
    pub const fn new(y: i32, m: u8, d: u8) -> Self {
        Self { y, m, d }
    }

    /// Days since 1970-01-01 (may be negative).
    pub fn to_days(self) -> i64 {
        days_from_civil(self.y, self.m, self.d)
    }

    /// Whole days from `self` to `other` (positive when `other` is later).
    pub fn days_since(self, other: Date) -> i64 {
        other.to_days() - self.to_days()
    }

    pub fn from_days(z: i64) -> Self {
        let (y, m, d) = civil_from_days(z);
        Self { y, m, d }
    }
}

pub fn days_from_civil(y: i32, m: u8, d: u8) -> i64 {
    let y = y - if m <= 2 { 1 } else { 0 };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = (y - era * 400) as i64; // [0, 399]
    let mp = ((m as i64) + 9) % 12; // [0, 11], March = 0
    let doy = (153 * mp + 2) / 5 + (d as i64) - 1; // [0, 365]
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; // [0, 146096]
    era as i64 * 146097 + doe - 719468
}

pub fn civil_from_days(z: i64) -> (i32, u8, u8) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = z - era * 146097; // [0, 146096]
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    let mp = (5 * doy + 2) / 153; // [0, 11]
    let d = (doy - (153 * mp + 2) / 5 + 1) as u8; // [1, 31]
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u8; // [1, 12]
    ((y + if m <= 2 { 1 } else { 0 }) as i32, m, d)
}

/// Parse a strict `YYYY-MM-DD` string.
pub fn parse_date(s: &str) -> Option<Date> {
    let b = s.as_bytes();
    if b.len() != 10 || b[4] != b'-' || b[7] != b'-' {
        return None;
    }
    let num = |r: core::ops::Range<usize>| -> Option<i64> {
        core::str::from_utf8(&b[r]).ok()?.parse().ok()
    };
    let y = num(0..4)?;
    let m = num(5..7)?;
    let d = num(8..10)?;
    if !(1..=12).contains(&m) || !(1..=31).contains(&d) {
        return None;
    }
    Some(Date::new(y as i32, m as u8, d as u8))
}

/// Parse a strict `HH:MM:SS` string (24-hour).
pub fn parse_hms(s: &str) -> Option<(u8, u8, u8)> {
    let b = s.as_bytes();
    if b.len() != 8 || b[2] != b':' || b[5] != b':' {
        return None;
    }
    let num = |r: core::ops::Range<usize>| -> Option<u8> {
        core::str::from_utf8(&b[r]).ok()?.parse().ok()
    };
    let hh = num(0..2)?;
    let mm = num(3..5)?;
    let ss = num(6..8)?;
    if hh > 23 || mm > 59 || ss > 59 {
        return None;
    }
    Some((hh, mm, ss))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unix_epoch_is_day_zero() {
        assert_eq!(days_from_civil(1970, 1, 1), 0);
        assert_eq!(Date::from_days(0), Date::new(1970, 1, 1));
    }

    #[test]
    fn known_dates() {
        assert_eq!(days_from_civil(2000, 3, 1), 11017);
        assert_eq!(days_from_civil(2024, 2, 29), 19782);
        assert_eq!(Date::from_days(19782), Date::new(2024, 2, 29));
    }

    #[test]
    fn leap_year_februaries() {
        // Feb 28 -> Mar 1 jumps one day in leap years AND non-leap years
        assert_eq!(days_from_civil(2024, 3, 1) - days_from_civil(2024, 2, 28), 2); // 29th exists
        assert_eq!(days_from_civil(2025, 3, 1) - days_from_civil(2025, 2, 28), 1);
    }

    #[test]
    fn anniversary_diff() {
        let anniv = Date::new(2025, 2, 14);
        // one year later, incl. the non-leap Feb 2025
        assert_eq!(anniv.days_since(Date::new(2026, 2, 14)), 365);
        assert_eq!(anniv.days_since(Date::new(2026, 2, 15)), 366);
        // same date = day 0
        assert_eq!(anniv.days_since(anniv), 0);
    }

    #[test]
    fn roundtrip_over_a_century() {
        // every 11 days across 1900-2100 roundtrips through the packing
        let mut z = days_from_civil(1900, 1, 1);
        let end = days_from_civil(2100, 1, 1);
        while z < end {
            let d = Date::from_days(z);
            assert_eq!(d.to_days(), z, "roundtrip failed at {:?}", d);
            z += 11;
        }
    }

    #[test]
    fn month_rollover() {
        assert_eq!(Date::from_days(days_from_civil(2026, 1, 31) + 1), Date::new(2026, 2, 1));
        assert_eq!(Date::from_days(days_from_civil(2026, 12, 31) + 1), Date::new(2027, 1, 1));
    }

    #[test]
    fn parse_date_strict() {
        assert_eq!(parse_date("2025-02-14"), Some(Date::new(2025, 2, 14)));
        assert_eq!(parse_date("2025-2-14"), None); // must be zero-padded
        assert_eq!(parse_date("2025-13-01"), None);
        assert_eq!(parse_date("2025-00-10"), None);
        assert_eq!(parse_date("2025/02/14"), None);
        assert_eq!(parse_date(""), None);
    }

    #[test]
    fn parse_hms_strict() {
        assert_eq!(parse_hms("08:30:00"), Some((8, 30, 0)));
        assert_eq!(parse_hms("23:59:59"), Some((23, 59, 59)));
        assert_eq!(parse_hms("24:00:00"), None);
        assert_eq!(parse_hms("8:30:00"), None);
    }
}
