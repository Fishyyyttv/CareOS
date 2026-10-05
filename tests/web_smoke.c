/* Opt-in test on the actual freestanding runtime and desktop boot stack. */
#define CAREOS_WEB_TEST
#include "host/test_web_js.c"

void web_selftest(u64 mbi) {
    bool requested = false;
    u32 size = *(u32 *)(uintptr_t)mbi;
    for (u64 p = mbi + 8; p + 8 <= mbi + size;) {
        u32 *tag = (u32 *)(uintptr_t)p;
        if (!tag[0] || tag[1] < 8) break;
        if (tag[0] == 1 && !kstrcmp((const char *)(tag + 2), "web-selftest")) requested = true;
        p += (tag[1] + 7u) & ~7u;
    }
    if (!requested) return;
    serial_write("[web-test] starting\n");
    __asm__ volatile("sti"); /* the execution budget requires PIT ticks */
    web_test_main();
    serial_write("[web-test] ALL PASSED\n");
    outl(0xf4, 0);
    for (;;) __asm__ volatile("cli; hlt");
}
