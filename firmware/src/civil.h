// molly-count — proleptic Gregorian calendar math (Howard Hinnant's
// days_from_civil). Ported from src/civil.rs (Rust firmware); day 0 =
// 1970-01-01. Pure logic, host-testable.
#pragma once

#include <stdint.h>

typedef struct {
    int32_t y;
    uint8_t m;
    uint8_t d;
} Date;

typedef struct {
    Date date;
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
} DateTime;

int64_t days_from_civil(int32_t y, uint8_t m, uint8_t d);

void civil_from_days(int64_t z, Date *out);

// Whole days from `from` to `to` (positive when `to` is later).
int64_t date_days_since(Date from, Date to);

// "Day N" count with the anniversary itself as day 1.
int64_t date_day_number(Date anniv, Date today);

// Strict "YYYY-MM-DD" (zero-padded, m 1..=12, d 1..=31). 1 ok / 0 fail.
int parse_date(const char *s, Date *out);

// Strict "HH:MM:SS" (24-hour). 1 ok / 0 fail.
int parse_hms(const char *s, uint8_t *hh, uint8_t *mm, uint8_t *ss);
