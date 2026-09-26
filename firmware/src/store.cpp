// Ported from src/store.rs — byte layout identical (records written by the
// Rust firmware must keep unpacking).
#include "store.h"

#include <string.h>

static const uint8_t MAGIC[4] = { 'M', 'C', 'V', '1' };

static uint32_t fnv1a(const uint8_t *data, size_t len)
{
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)data[i];
        h *= 0x01000193u;
    }
    return h;
}

void record_placeholder(Record *r)
{
    r->anniv.y = 2025;
    r->anniv.m = 2;
    r->anniv.d = 14;
    r->last_shown_days = RECORD_NEVER_SHOWN;
    r->screen_kind = 0;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

void record_pack(const Record *r, uint8_t b[RECORD_LEN])
{
    memset(b, 0, RECORD_LEN);
    memcpy(&b[0], MAGIC, 4);
    put_le32(&b[4], (uint32_t)r->anniv.y);
    b[8] = r->anniv.m;
    b[9] = r->anniv.d;
    put_le64(&b[10], (uint64_t)r->last_shown_days);
    put_le32(&b[20], fnv1a(&b[0], 20));
    b[24] = r->screen_kind;
}

int record_unpack(const uint8_t b[RECORD_LEN], Record *out)
{
    if (memcmp(&b[0], MAGIC, 4) != 0) return 0;
    uint32_t sum = get_le32(&b[20]);
    if (sum != fnv1a(&b[0], 20)) return 0;
    int32_t y = (int32_t)get_le32(&b[4]);
    uint8_t m = b[8];
    uint8_t d = b[9];
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    out->anniv.y = y;
    out->anniv.m = m;
    out->anniv.d = d;
    out->last_shown_days = (int64_t)get_le64(&b[10]);
    out->screen_kind = b[24];
    return 1;
}
