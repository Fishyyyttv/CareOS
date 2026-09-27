/* tests/host/test_netrate.c -- NetMon throughput from byte counters. */
#include "pure.h"
#include "netrate.h"
#include <assert.h>
int main(void) {
    assert(net_rate_kbs(1000, 1000, 200) == 0);          /* idle */
    assert(net_rate_kbs(0, 20000, 200) == 100);          /* 20 KB in 200 ms = 100 KB/s */
    assert(net_rate_kbs(0xFFFFFF00u, 0x00000F00u, 100) == 40);   /* counter wrapped: 4096 B / 100 ms */
    assert(net_rate_kbs(0, 5000, 0) == 5000);            /* dt 0 treated as 1 ms, no divide by zero */
    assert(net_rate_kbs(0, 0xFFFFFFFFu, 1) <= 0xFFFFFFFFu);
    assert(net_rate_cap8(300) == 255 && net_rate_cap8(12) == 12);
    puts("netrate OK");
    return 0;
}
