/* tests/host/test_pip_assist.c -- what Pip understands. */
#include "pure.h"
#include "pip_assist.h"
#include "settings_index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static pip_parsed_t P(const char *s) { pip_parsed_t o; pip_assist_parse(s, &o); return o; }
#define IS(s, k, a) do { pip_parsed_t o = P(s); if (o.kind != (k) || o.arg != (a)) { printf("FAIL: \"%s\" -> %d/%d, want %d/%d\n", s, o.kind, o.arg, (int)(k), (int)(a)); return 1; } } while (0)

int main(void) {
    IS("", PI_EMPTY, 0);
    IS("   ", PI_EMPTY, 0);
    IS("Open Settings", PI_OPEN_APP, PA_SETTINGS);
    IS("open settings!", PI_OPEN_APP, PA_SETTINGS);
    IS("can you please launch the terminal?", PI_OPEN_APP, PA_TERMINAL);
    IS("files", PI_OPEN_APP, PA_FILES);
    IS("open text editor", PI_OPEN_APP, PA_EDITOR);
    IS("open network monitor", PI_OPEN_APP, PA_NETMON);
    IS("open the package manager", PI_OPEN_APP, PA_PKGMGR);
    IS("play doom", PI_OPEN_APP, PA_DOOM);
    IS("open pip home", PI_OPEN_APP, PA_PIP);
    IS("how do I open the terminal", PI_OPEN_APP, PA_TERMINAL);
    /* settings pages */
    IS("change the wallpaper", PI_OPEN_PAGE, SET_APPEARANCE);
    IS("open wifi settings", PI_OPEN_PAGE, SET_NETWORK);
    IS("pip settings", PI_OPEN_PAGE, SET_PIP);
    IS("go to sound settings", PI_OPEN_PAGE, SET_SOUND);
    IS("where do I change my password", PI_OPEN_PAGE, SET_ACCOUNTS);
    IS("adjust brightness", PI_OPEN_PAGE, SET_DISPLAY);
    IS("set the time zone", PI_OPEN_PAGE, SET_DATETIME);
    /* actions */
    IS("dark mode", PI_THEME, 0);
    IS("switch to light theme", PI_THEME, 1);
    IS("make it dark", PI_THEME, 0);
    IS("turn on do not disturb", PI_DND, 1);
    IS("turn off dnd", PI_DND, 0);
    IS("lock the screen", PI_LOCK, 0);
    IS("lock", PI_LOCK, 0);
    IS("minimize all windows", PI_MIN_ALL, 0);
    IS("close all windows", PI_CLOSE_ALL, 0);
    /* facts */
    IS("what time is it", PI_INFO, PINF_TIME);
    IS("what's the date", PI_INFO, PINF_DATE);
    IS("how much memory is free", PI_INFO, PINF_MEMORY);
    IS("who am i", PI_INFO, PINF_USER);
    IS("am I online?", PI_INFO, PINF_NETWORK);
    IS("what's my IP", PI_INFO, PINF_NETWORK);
    IS("system info", PI_INFO, PINF_SYSTEM);
    IS("what windows are open", PI_INFO, PINF_WINDOWS);
    IS("how many packages are installed", PI_INFO, PINF_PACKAGES);
    /* how-to */
    IS("How do I install a package?", PI_EXPLAIN, PT_PACKAGES);
    IS("how do I write a care script", PI_EXPLAIN, PT_CARE);
    IS("what are the keyboard shortcuts", PI_EXPLAIN, PT_SHORTCUTS);
    IS("how do I lock the screen", PI_EXPLAIN, PT_LOCK);
    IS("how do I delete a file", PI_EXPLAIN, PT_FILES);
    IS("i'm new here", PI_EXPLAIN, PT_TOUR);
    /* finding files */
    { pip_parsed_t o = P("find my budget file"); assert(o.kind == PI_FIND && !strcmp(o.text, "budget")); }
    { pip_parsed_t o = P("where is notes.txt"); assert(o.kind == PI_FIND && !strcmp(o.text, "notes.txt")); }
    { pip_parsed_t o = P("find a file"); assert(o.kind == PI_FIND && o.text[0] == '\0'); }
    /* things he refuses or can't do honestly */
    IS("delete my files", PI_DELETE, 0);
    IS("shut down", PI_POWER, 0);
    IS("log out", PI_POWER, 1);
    IS("what's the weather", PI_UNSUPPORTED, PN_WEATHER);
    IS("battery level", PI_UNSUPPORTED, PN_BATTERY);
    IS("search the web for cats", PI_UNSUPPORTED, PN_WEB_SEARCH);
    IS("play some music", PI_UNSUPPORTED, PN_MEDIA);
    /* Pip himself */
    IS("hello", PI_GREET, 0);
    IS("hey pip", PI_GREET, 0);
    IS("what can you do", PI_CAPS, 0);
    IS("help", PI_CAPS, 0);
    IS("who are you", PI_ABOUT_PIP, 0);
    IS("how are you", PI_HOW_ARE_YOU, 0);
    IS("thanks!", PI_THANKS, 0);
    IS("pat", PI_NONE, 0);   // a bare "pat" is not enough; "pat you" is
    IS("pat you", PI_PAT, 0);
    IS("go play", PI_PLAY, 0);
    IS("take a nap", PI_NAP, 0);
    IS("come back", PI_COME_BACK, 0);
    IS("dance", PI_DANCE, 0);
    IS("turn off tips", PI_TIPS, 0);
    IS("turn on tips", PI_TIPS, 1);
    IS("hide yourself", PI_HIDE, 0);
    IS("turn off pip", PI_DISABLE, 0);
    IS("give me a tip", PI_TIP, 0);
    IS("rename yourself", PI_RENAME, 0);
    /* confirmations only when that's all they said */
    IS("yes", PI_YES, 0);
    IS("yes please", PI_YES, 0);
    IS("do it", PI_YES, 0);
    IS("no", PI_NO, 0);
    IS("never mind", PI_NO, 0);
    IS("stop tips", PI_TIPS, 0);
    /* nonsense */
    IS("flibbertigibbet", PI_NONE, 0);

    /* every app / topic / nope / quick action has words */
    for (u32 i = 0; i < PA_COUNT; i++) assert(strlen(pip_app_name(i)) > 2);
    for (u32 i = 0; i < PT_COUNT; i++) assert(strlen(pip_topic_text(i)) > 20 && strlen(pip_topic_title(i)) > 2);
    for (u32 i = 0; i < PN_COUNT; i++) assert(strlen(pip_nope_text(i)) > 20);
    for (u32 i = 0; i < pip_quick_count(); i++) { assert(*pip_quick_label(i)); assert(P(pip_quick_text(i)).kind != PI_NONE); }
    assert(strstr(pip_tip_for(PA_TERMINAL, 0), "Tip"));
    assert(strstr(pip_tip_for(-1, 7), "Tip"));
    printf("pip_assist ok\n");
    return 0;
}
