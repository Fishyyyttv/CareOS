/* gui/textbuf.c -- geometric growth for heap text buffers (pure; host-tested). */
#include "textbuf.h"

bool tb_reserve(char **buf, u32 *cap, u32 need, u32 max) {
    if (need > max) return false;
    if (*buf && need <= *cap) return true;
    u32 nc = *cap ? *cap : 256;
    if (nc > max) nc = max;
    while (nc < need) nc = (nc > max / 2) ? max : nc * 2;
    char *nb = (char *)kmalloc(nc);
    if (!nb) return false;
    if (*buf) { kmemcpy(nb, *buf, *cap); kfree(*buf); }
    else nb[0] = '\0';
    *buf = nb; *cap = nc;
    return true;
}
