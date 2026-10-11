/* gui/pip/pip_assist.h -- what Pip understands (pure, host-tested).
 * A small rule-based language layer: free text in, one intent out. It knows
 * nothing about windows or the disk; pip_chat.c turns an intent into a real
 * OS action and a reply. A smarter backend can replace pip_assist_parse()
 * later without touching anything else -- it only has to fill pip_parsed_t. */
#ifndef CAREOS_PIP_ASSIST_H
#define CAREOS_PIP_ASSIST_H
#include "pure.h"

typedef enum {
    PI_NONE = 0,                       /* not understood */
    PI_EMPTY,
    PI_GREET, PI_THANKS, PI_BYE, PI_HOW_ARE_YOU, PI_ABOUT_PIP, PI_CAPS, PI_TIP, PI_RENAME,
    PI_OPEN_APP,                       /* arg = pip_app_t */
    PI_OPEN_PAGE,                      /* arg = Settings section */
    PI_FIND,                           /* text = name to look for ("" = ask) */
    PI_INFO,                           /* arg = pip_info_t */
    PI_EXPLAIN,                        /* arg = pip_topic_t */
    PI_THEME,                          /* arg = 0 dark, 1 light, 2 auto */
    PI_DND,                            /* arg = 1 on, 0 off */
    PI_LOCK, PI_MIN_ALL, PI_CLOSE_ALL,
    PI_PAT, PI_PLAY, PI_COME_BACK, PI_NAP, PI_DANCE,
    PI_HIDE, PI_DISABLE,
    PI_TIPS,                           /* arg = 1 on, 0 off */
    PI_DELETE, PI_POWER, PI_UNSUPPORTED,   /* arg = pip_nope_t */
    PI_YES, PI_NO
} pip_intent_t;

typedef enum {
    PA_TERMINAL = 0, PA_NOTES, PA_FILES, PA_SYSMON, PA_CALC, PA_ABOUT, PA_HELP, PA_BROWSER,
    PA_SETTINGS, PA_PKGMGR, PA_EDITOR, PA_PAINT, PA_CLOCK, PA_NETMON, PA_USERS, PA_MAZE,
    PA_3D, PA_DOOM, PA_PIP, PA_COUNT
} pip_app_t;

typedef enum {
    PINF_TIME = 0, PINF_DATE, PINF_MEMORY, PINF_UPTIME, PINF_USER, PINF_SYSTEM, PINF_NETWORK,
    PINF_WINDOWS, PINF_PACKAGES, PINF_COUNT
} pip_info_t;

typedef enum {
    PT_TOUR = 0, PT_PACKAGES, PT_TERMINAL, PT_CARE, PT_FILES, PT_NOTES, PT_BROWSER, PT_THEME,
    PT_LOCK, PT_SHORTCUTS, PT_WORKSPACES, PT_LAUNCHER, PT_SNAP, PT_NETWORK, PT_ACCOUNTS, PT_PIP,
    PT_SETTINGS, PT_COUNT
} pip_topic_t;

typedef enum { PN_WEATHER = 0, PN_BATTERY, PN_WEB_SEARCH, PN_MEDIA, PN_COUNT } pip_nope_t;

typedef struct {
    pip_intent_t kind;
    i32          arg;
    u8           flag;                 /* PI_OPEN_APP: 1 = the user asked to search the web */
    char         text[32];             /* PI_FIND: the word to look for */
} pip_parsed_t;

void        pip_assist_parse(const char *input, pip_parsed_t *out);

const char *pip_app_name(u32 app);                  /* "Terminal" */
const char *pip_topic_title(u32 topic);
const char *pip_topic_text(u32 topic);              /* a few plain sentences */
const char *pip_nope_text(u32 nope);
/* Quick-action chips in Pip's Home: label and the text it sends. */
u32         pip_quick_count(void);
const char *pip_quick_label(u32 i);
const char *pip_quick_text(u32 i);
/* A tip for an app that has the user's attention (app = pip_app_t, or -1 for the desktop). */
const char *pip_tip_for(i32 app, u32 salt);

#endif
