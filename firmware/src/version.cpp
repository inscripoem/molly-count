#include "version.h"

#include <stdlib.h>

int fw_ver_cmp(const char *a, const char *b)
{
    unsigned long va, vb;
    while (*a || *b) {
        va = strtoul(a, (char **)&a, 10);
        vb = strtoul(b, (char **)&b, 10);
        if (va != vb) return (va < vb) ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}
