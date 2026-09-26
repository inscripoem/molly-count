// Ported from src/civil.rs — algorithm identical, test cases ported too.
#include "civil.h"

#include <string.h>

static int digit_val(char c, int64_t *out)
{
    if (c < '0' || c > '9') return 0;
    *out = *out * 10 + (c - '0');
    return 1;
}

int64_t days_from_civil(int32_t y, uint8_t m, uint8_t d)
{
    y -= (m <= 2) ? 1 : 0;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                        // [0, 399]
    int64_t mp = ((int64_t)m + 9) % 12;                 // [0, 11], March = 0
    int64_t doy = (153 * mp + 2) / 5 + (int64_t)d - 1;  // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;// [0, 146096]
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, Date *out)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                  // [0, 146096]
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);           // [0, 365]
    int64_t mp = (5 * doy + 2) / 153;                                // [0, 11]
    int64_t d = doy - (153 * mp + 2) / 5 + 1;                        // [1, 31]
    int64_t m = (mp < 10) ? mp + 3 : mp - 9;                         // [1, 12]
    out->y = (int32_t)(y + ((m <= 2) ? 1 : 0));
    out->m = (uint8_t)m;
    out->d = (uint8_t)d;
}

int64_t date_days_since(Date from, Date to)
{
    return days_from_civil(to.y, to.m, to.d) - days_from_civil(from.y, from.m, from.d);
}

int64_t date_day_number(Date anniv, Date today)
{
    return date_days_since(anniv, today) + 1;
}

int parse_date(const char *s, Date *out)
{
    if (s == NULL || strlen(s) != 10) return 0;
    if (s[4] != '-' || s[7] != '-') return 0;
    int64_t y = 0, m = 0, d = 0;
    for (int i = 0; i < 4; i++) if (!digit_val(s[i], &y)) return 0;
    for (int i = 5; i < 7; i++) if (!digit_val(s[i], &m)) return 0;
    for (int i = 8; i < 10; i++) if (!digit_val(s[i], &d)) return 0;
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    out->y = (int32_t)y;
    out->m = (uint8_t)m;
    out->d = (uint8_t)d;
    return 1;
}

int parse_hms(const char *s, uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    if (s == NULL || strlen(s) != 8) return 0;
    if (s[2] != ':' || s[5] != ':') return 0;
    int64_t h = 0, m = 0, sec = 0;
    if (!digit_val(s[0], &h) || !digit_val(s[1], &h)) return 0;
    if (!digit_val(s[3], &m) || !digit_val(s[4], &m)) return 0;
    if (!digit_val(s[6], &sec) || !digit_val(s[7], &sec)) return 0;
    if (h > 23 || m > 59 || sec > 59) return 0;
    *hh = (uint8_t)h;
    *mm = (uint8_t)m;
    *ss = (uint8_t)sec;
    return 1;
}
