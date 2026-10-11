/* tests/host/test_settings_index.c -- Settings sidebar search filter. */
#include "pure.h"
#include "settings_index.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    u32 out[SET_COUNT];
    /* empty and blank queries list everything */
    assert(settings_filter("", out, SET_COUNT) == SET_COUNT);
    assert(settings_filter("   ", out, SET_COUNT) == SET_COUNT);
    assert(settings_filter(NULL, out, SET_COUNT) == SET_COUNT);
    /* title match, case-insensitive */
    assert(settings_filter("RESOLUTION", out, SET_COUNT) == 1 && out[0] == SET_DISPLAY);
    assert(settings_filter("Display", out, SET_COUNT) == 2 && out[0] == SET_DISPLAY && out[1] == SET_ACCOUNTS);
    /* keyword match */
    assert(settings_filter("wallpaper", out, SET_COUNT) == 1 && out[0] == SET_APPEARANCE);
    assert(settings_filter("dns", out, SET_COUNT) == 1 && out[0] == SET_NETWORK);
    assert(settings_filter("password", out, SET_COUNT) == 1 && out[0] == SET_ACCOUNTS);
    /* partial words match */
    assert(settings_filter("bright", out, SET_COUNT) == 1 && out[0] == SET_DISPLAY);
    /* every word must match */
    assert(settings_filter("dark theme", out, SET_COUNT) == 1 && out[0] == SET_APPEARANCE);
    assert(settings_filter("dark disk", out, SET_COUNT) == 0);
    /* several sections, in order */
    u32 n = settings_filter("time", out, SET_COUNT);
    assert(n == 2 && out[0] == SET_DATETIME && out[1] == SET_ABOUT);   /* "uptime" */
    /* no match */
    assert(settings_filter("bluetooth", out, SET_COUNT) == 0);
    /* max caps writes but not the count */
    assert(settings_filter("", out, 2) == SET_COUNT);
    assert(!settings_section_matches(SET_COUNT, ""));
    puts("settings_index OK");
    return 0;
}
