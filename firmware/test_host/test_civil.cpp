// Ported from src/civil.rs #[cfg(test)] — same cases.
#include <string.h>

#include "civil.h"
#include "test_harness.h"

static Date D(int y, int m, int d)
{
    Date dt;
    dt.y = y;
    dt.m = (uint8_t)m;
    dt.d = (uint8_t)d;
    return dt;
}

static int64_t days_since(Date from, Date to)
{
    return date_days_since(from, to);
}

void run_civil_tests()
{
    // unix_epoch_is_day_zero
    CHECK(days_from_civil(1970, 1, 1) == 0);
    Date e;
    civil_from_days(0, &e);
    CHECK(e.y == 1970 && e.m == 1 && e.d == 1);

    // known_dates
    CHECK(days_from_civil(2000, 3, 1) == 11017);
    CHECK(days_from_civil(2024, 2, 29) == 19782);
    civil_from_days(19782, &e);
    CHECK(e.y == 2024 && e.m == 2 && e.d == 29);

    // leap_year_februaries
    CHECK(days_from_civil(2024, 3, 1) - days_from_civil(2024, 2, 28) == 2);
    CHECK(days_from_civil(2025, 3, 1) - days_from_civil(2025, 2, 28) == 1);

    // anniversary_diff
    Date anniv = D(2025, 2, 14);
    CHECK(days_since(anniv, D(2026, 2, 14)) == 365);
    CHECK(days_since(anniv, D(2026, 2, 15)) == 366);
    CHECK(days_since(anniv, anniv) == 0);

    // day_number_counts_anniversary_as_day_one
    Date a2 = D(2026, 5, 16);
    CHECK(date_day_number(a2, a2) == 1);
    CHECK(date_day_number(a2, D(2026, 5, 17)) == 2);
    CHECK(date_day_number(a2, D(2026, 9, 14)) == 122);

    // roundtrip_over_a_century
    for (int64_t z = days_from_civil(1900, 1, 1); z < days_from_civil(2100, 1, 1);
         z += 11) {
        Date d;
        civil_from_days(z, &d);
        CHECK(days_from_civil(d.y, d.m, d.d) == z);
        if (g_check_fails) break;
    }

    // month_rollover
    civil_from_days(days_from_civil(2026, 1, 31) + 1, &e);
    CHECK(e.y == 2026 && e.m == 2 && e.d == 1);
    civil_from_days(days_from_civil(2026, 12, 31) + 1, &e);
    CHECK(e.y == 2027 && e.m == 1 && e.d == 1);

    // parse_date_strict
    Date pd;
    CHECK(parse_date("2025-02-14", &pd) == 1 && pd.y == 2025 && pd.m == 2 && pd.d == 14);
    CHECK(parse_date("2025-2-14", &pd) == 0);
    CHECK(parse_date("2025-13-01", &pd) == 0);
    CHECK(parse_date("2025-00-10", &pd) == 0);
    CHECK(parse_date("2025/02/14", &pd) == 0);
    CHECK(parse_date("", &pd) == 0);
    CHECK(parse_date(NULL, &pd) == 0);

    // parse_hms_strict
    uint8_t hh, mm, ss;
    CHECK(parse_hms("08:30:00", &hh, &mm, &ss) == 1 && hh == 8 && mm == 30 && ss == 0);
    CHECK(parse_hms("23:59:59", &hh, &mm, &ss) == 1 && hh == 23 && mm == 59 && ss == 59);
    CHECK(parse_hms("24:00:00", &hh, &mm, &ss) == 0);
    CHECK(parse_hms("8:30:00", &hh, &mm, &ss) == 0);
}
