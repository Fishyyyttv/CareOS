/* gui/pip/pip_chat.c -- Pip's conversation and the real actions behind it.
 * Every reply states only what actually happened or what CareOS really has. */
#include "pip.h"
#include "pip_chat.h"
#include "pip_assist.h"
#include "../../apps/apps_common.h"
#include "../../apps/settings_index.h"
#include "../session/session.h"

extern void term_ip_to_str(u32 ip, char *out);

static pip_msg_t g_msgs[PIP_CHAT_MAX];
static u32  g_count, g_serial;
static bool g_busy;
static u32  g_due_ms;
static char g_pending_text[160];

typedef enum { CF_NONE = 0, CF_CLOSE_ALL, CF_DISABLE } confirm_t;
static confirm_t g_confirm;
static u32       g_confirm_until;

u32 pip_chat_count(void) { return g_count; }
const pip_msg_t *pip_chat_get(u32 i) { return i < g_count ? &g_msgs[i] : NULL; }
u32 pip_chat_serial(void) { return g_serial; }
bool pip_chat_busy(void) { return g_busy; }
bool pip_chat_waiting_confirm(void) { return g_confirm != CF_NONE && timer_ms() < g_confirm_until; }

static void push(bool pip, const char *text) {
    if (g_count == PIP_CHAT_MAX) {
        for (u32 i = 1; i < PIP_CHAT_MAX; i++) g_msgs[i - 1] = g_msgs[i];
        g_count--;
    }
    pip_msg_t *m = &g_msgs[g_count++];
    m->pip = pip;
    kstrncpy(m->text, text, PIP_MSG_LEN - 1);
    m->text[PIP_MSG_LEN - 1] = '\0';
    g_serial++;
}
void pip_chat_pip_says(const char *text) { push(true, text); }

void pip_chat_reset(void) { g_count = 0; g_busy = false; g_confirm = CF_NONE; g_serial++; }

void pip_chat_intro(void) {
    if (g_count || !pip_brain()->enabled) return;
    char b[PIP_MSG_LEN];
    kstrcpy(b, "Hi, I'm "); kstrcat(b, pip_brain()->name);
    kstrcat(b, "! I live on your desktop and I can open apps, jump to a Settings page, find files, tell you about this computer and explain how CareOS works. "
               "Try \"open notes\" or \"what can you do?\" -- or tap one of the buttons below.");
    push(true, b);
    pip_event(PE_GREET);
    pip_brain()->intro_seen = true;
    pip_mark_dirty();
}

/* ---- opening things ------------------------------------------------------- */
static const struct { app_id_t app; const char *title; } APPMAP[PA_COUNT] = {
    [PA_TERMINAL] = { APP_TERMINAL, "Terminal" },   [PA_NOTES]  = { APP_NOTES, "Notes" },
    [PA_FILES]    = { APP_FILES, "Files" },         [PA_SYSMON] = { APP_SYSMON, "System Monitor" },
    [PA_CALC]     = { APP_CALC, "Calculator" },     [PA_ABOUT]  = { APP_ABOUT, "About" },
    [PA_HELP]     = { APP_HELP, "Help" },           [PA_BROWSER] = { APP_BROWSER, "Browser" },
    [PA_SETTINGS] = { APP_SETTINGS, "Settings" },   [PA_PKGMGR] = { APP_PKGMGR, "Packages" },
    [PA_EDITOR]   = { APP_EDITOR, "Editor" },       [PA_PAINT]  = { APP_PAINT, "Paint" },
    [PA_CLOCK]    = { APP_CLOCK, "Clock" },         [PA_NETMON] = { APP_NETMON, "Network Monitor" },
    [PA_USERS]    = { APP_USERS, "Users" },         [PA_MAZE]   = { APP_MAZE, "Maze" },
    [PA_3D]       = { APP_3D, "3D Demo" },          [PA_DOOM]   = { APP_DOOM, "DOOM" },
    [PA_PIP]      = { APP_PIP, "Pip's Home" },
};

window_t *pip_open_app(u32 pa) {
    if (pa >= PA_COUNT) return NULL;
    app_id_t app = APPMAP[pa].app;
    window_t *w = wm_find_app(app);
    if (w) {
        if (w->minimized) wm_minimize(w);                 /* minimized -> restore */
        wm_focus(w);
        return w;
    }
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H, ww, wh;
    app_default_size(app, sw, sh, &ww, &wh);
    return wm_open(app, APPMAP[pa].title, (sw - ww) / 2, (sh - wh) / 2 - 10, ww, wh);
}

/* ---- small formatting helpers ------------------------------------------------ */
static void cat_u(char *b, u32 v) { char d[12]; kutoa(v, d, 10); kstrcat(b, d); }
static void cat_mib(char *b, u32 bytes) { cat_u(b, bytes / (1024u * 1024u)); kstrcat(b, " MiB"); }
static u32 salt(void) { return timer_ms() * 2654435761u >> 16; }
static const char *pick(const char *const *opts, u32 n) { return opts[salt() % n]; }

/* ---- searching the disk for a name ----------------------------------------- */
typedef struct { char path[96]; bool dir; } hit_t;
static hit_t g_hits[5];
static u32 g_nhits, g_visited;

static bool name_has(const char *name, const char *q) {
    for (u32 i = 0; name[i]; i++) {
        u32 k = 0;
        while (q[k]) {
            char a = name[i + k], b = q[k];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            k++;
        }
        if (!q[k]) return true;
    }
    return false;
}
static void walk(fs_node_t *dir, const char *q, u32 depth) {
    if (!dir || dir->type != FS_DIR || depth > 6 || g_visited > 3000) return;
    vfs_find(dir, "\x01");                               /* loads an ext2 directory's entries */
    for (u32 i = 0; i < dir->child_count && g_nhits < 5; i++) {
        fs_node_t *c = dir->children[i];
        if (!c) continue;
        g_visited++;
        if (name_has(c->name, q)) {
            hit_t *h = &g_hits[g_nhits++];
            vfs_get_path(c, h->path, sizeof h->path);
            h->dir = c->type == FS_DIR;
        }
        if (c->type == FS_DIR) walk(c, q, depth + 1);
    }
}

/* ---- the actions -------------------------------------------------------------- */
static void say_short(const char *s) { pip_say(s, 2600); }

static void do_find(const pip_parsed_t *p, char *r) {
    if (!p->text[0]) { kstrcpy(r, "What should I look for? Try \"find notes\" or \"where is report.txt\"."); pip_event(PE_THINK); return; }
    g_nhits = 0; g_visited = 0;
    char home[64]; app_home_path(home);
    walk(vfs_resolve_path(home), p->text, 0);          /* your home folder first */
    if (g_nhits < 5) walk(vfs_root(), p->text, 0);
    if (!g_nhits) {
        kstrcpy(r, "I looked through your folders and found nothing with \""); kstrcat(r, p->text); kstrcat(r, "\" in its name.");
        pip_event(PE_OOPS); say_short("No luck...");
        return;
    }
    kstrcpy(r, "Found "); cat_u(r, g_nhits); kstrcat(r, g_nhits == 1 ? " match:" : " matches:");
    for (u32 i = 0; i < g_nhits; i++) { kstrcat(r, "\n"); kstrcat(r, g_hits[i].path); if (g_hits[i].dir) kstrcat(r, "/"); }
    kstrcat(r, "\nI'll open Files for you.");
    pip_open_app(PA_FILES);
    pip_event(PE_DONE); say_short("Found it!");
}

static void do_info(u32 which, char *r) {
    r[0] = '\0';
    switch (which) {
    case PINF_TIME: {
        rtc_time_t t; clock_read_local(&t);
        const careos_settings_t *cfg = settings_get();
        kstrcpy(r, "It's ");
        u32 h = t.hour;
        bool pm = h >= 12;
        if (!cfg->clock_24h) { h = h % 12; if (!h) h = 12; }
        cat_u(r, h); kstrcat(r, ":"); if (t.minute < 10) kstrcat(r, "0"); cat_u(r, t.minute);
        if (!cfg->clock_24h) kstrcat(r, pm ? " PM" : " AM");
        kstrcat(r, ".");
        break;
    }
    case PINF_DATE: { char d[40]; session_date_line(d, true); kstrcpy(r, "Today is "); kstrcat(r, d); kstrcat(r, "."); break; }
    case PINF_MEMORY:
        kstrcpy(r, "This computer has ");
        if (pmm_ram_mib()) { cat_u(r, pmm_ram_mib()); kstrcat(r, " MiB of RAM. "); } else kstrcat(r, "an unknown amount of RAM. ");
        kstrcat(r, "The system heap has "); cat_mib(r, kmem_used()); kstrcat(r, " in use and "); cat_mib(r, kmem_free_bytes()); kstrcat(r, " free.");
        break;
    case PINF_UPTIME: {
        u32 s = timer_ms() / 1000, m = s / 60, h = m / 60;
        kstrcpy(r, "CareOS has been running for ");
        if (h) { cat_u(r, h); kstrcat(r, h == 1 ? " hour " : " hours "); }
        cat_u(r, m % 60); kstrcat(r, m % 60 == 1 ? " minute." : " minutes.");
        break;
    }
    case PINF_USER:
        kstrcpy(r, "You're signed in as "); kstrcat(r, user_current_name());
        kstrcat(r, user_is_root() ? ", an administrator." : ", a standard account.");
        if (!pip_saved()) kstrcat(r, " This is a guest session, so nothing is saved.");
        break;
    case PINF_SYSTEM: {
        kstrcpy(r, "CareOS v9.0 with the v10 shell, x86-64. Screen ");
        cat_u(r, SCREEN_W); kstrcat(r, "x"); cat_u(r, SCREEN_H); kstrcat(r, ". ");
        if (pmm_ram_mib()) { cat_u(r, pmm_ram_mib()); kstrcat(r, " MiB of RAM. "); }
        kstrcat(r, "Network link "); kstrcat(r, net_is_up() ? "up. " : "down. ");
        cat_u(r, carepkg_count()); kstrcat(r, " packages in the store. Open About for more.");
        break;
    }
    case PINF_NETWORK:
        if (net_is_up()) { char ip[20]; term_ip_to_str(net_get_ip(), ip); kstrcpy(r, "The network link is up, and your IP address is "); kstrcat(r, ip); kstrcat(r, "."); }
        else kstrcpy(r, "The network link is down right now. Settings > Network can run diagnostics or renew DHCP.");
        break;
    case PINF_WINDOWS: {
        u32 n = 0; char list[200]; list[0] = '\0';
        for (u32 i = 0; i < MAX_WINDOWS; i++) {
            window_t *w = wm_get_window((int)i);
            if (!w || !w->active || w->app == APP_PIP || !wm_on_current_desktop(w)) continue;
            if (n < 5) { if (n) kstrcat(list, ", "); kstrcat(list, w->title); }
            n++;
        }
        if (!n) kstrcpy(r, "Nothing is open on this desktop right now.");
        else { kstrcpy(r, "You have "); cat_u(r, n); kstrcat(r, n == 1 ? " window open: " : " windows open: "); kstrcat(r, list); if (n > 5) kstrcat(r, ", and more"); kstrcat(r, "."); }
        break;
    }
    case PINF_PACKAGES: {
        u32 total = carepkg_count(), inst = 0;
        for (u32 i = 0; i < total; i++) { char a[32], b[16], c[64], d[24]; bool in = false; if (carepkg_get_info(i, a, b, c, d, &in) && in) inst++; }
        kstrcpy(r, "The store has "); cat_u(r, total); kstrcat(r, total == 1 ? " package" : " packages"); kstrcat(r, ", and "); cat_u(r, inst); kstrcat(r, " installed.");
        break;
    }
    default: kstrcpy(r, "I'm not sure about that one.");
    }
    pip_event(PE_DONE);
    say_short("There you go!");
}

static void do_open(const pip_parsed_t *p, char *r) {
    window_t *w = pip_open_app((u32)p->arg);
    if (!w) { kstrcpy(r, "I couldn't open that -- there may be too many windows already. Close one and ask again."); pip_event(PE_OOPS); say_short("Couldn't open it"); return; }
    static const char *const LEAD[] = { "On it! Opening ", "Sure thing -- opening ", "Here's " };
    kstrcpy(r, pick(LEAD, 3)); kstrcat(r, pip_app_name((u32)p->arg)); kstrcat(r, ".");
    if (p->flag) kstrcat(r, " I can't search the web for you yet, but you can type an address in there.");
    pip_event(PE_ACTION);
    say_short("Done!");
}

static void do_page(const pip_parsed_t *p, char *r) {
    window_t *w = pip_open_app(PA_SETTINGS);
    if (!w) { kstrcpy(r, "I couldn't open Settings right now."); pip_event(PE_OOPS); return; }
    w->settings_tab = (u32)p->arg; w->scroll = 0; w->input_len = 0; w->input_buf[0] = '\0';
    kstrcpy(r, "Opening Settings > "); kstrcat(r, SETTINGS_SECTIONS[p->arg].title); kstrcat(r, ".");
    pip_event(PE_ACTION); say_short("Here you go!");
}

static void do_confirmed(confirm_t c, char *r) {
    if (c == CF_CLOSE_ALL) {
        wm_close_all();
        kstrcpy(r, "All windows closed.");
        pip_event(PE_DONE); say_short("All closed.");
    } else if (c == CF_DISABLE) {
        pip_brain_t *b = pip_brain();
        kstrcpy(r, "Okay. I'm turning myself off. Settings > Pip turns me back on. Bye for now!");
        b->enabled = false;
        pip_mark_dirty(); pip_save_now();
    }
}

static void run_intent(const pip_parsed_t *p, char *r) {
    pip_brain_t *b = pip_brain();
    r[0] = '\0';
    confirm_t pending = pip_chat_waiting_confirm() ? g_confirm : CF_NONE;
    if (p->kind != PI_YES && p->kind != PI_NO) g_confirm = CF_NONE;     /* anything else cancels a question */
    switch (p->kind) {
    case PI_EMPTY: kstrcpy(r, "I'm listening! Ask me something, or tap one of the buttons."); break;
    case PI_GREET: {
        user_profile_t prof; kmemset(&prof, 0, sizeof prof);
        bool have = user_profile_get(user_current_name(), &prof);
        const char *nm = have && prof.display_name[0] ? prof.display_name : user_current_name();
        static const char *const G[] = { "Hi ", "Hello ", "Hey " };
        kstrcpy(r, pick(G, 3)); kstrcat(r, nm); kstrcat(r, "! What can I do for you?");
        pip_event(PE_GREET); break;
    }
    case PI_THANKS: { static const char *const T[] = { "Anytime!", "Happy to help!", "You are welcome!" }; kstrcpy(r, pick(T, 3)); pip_event(PE_GREET); break; }
    case PI_BYE: kstrcpy(r, "See you soon! I'll be right here on your desktop."); pip_event(PE_CLOSE_HOME); break;
    case PI_HOW_ARE_YOU:
        kstrcpy(r, b->happy >= 60 ? "I'm doing great! " : b->happy >= 30 ? "I'm okay. " : "A little lonely, honestly. ");
        kstrcat(r, "Happiness "); cat_u(r, b->happy); kstrcat(r, "%, energy "); cat_u(r, b->energy); kstrcat(r, "%. A pat always helps!");
        pip_event(PE_GREET); break;
    case PI_ABOUT_PIP: kstrcpy(r, pip_topic_text(PT_PIP)); pip_event(PE_GREET); break;
    case PI_CAPS:
        kstrcpy(r, "Here's what I can really do:\n"
                   "- Open apps: \"open terminal\", \"launch files\"\n"
                   "- Jump to a Settings page: \"wallpaper settings\", \"change my password\"\n"
                   "- Find files: \"find notes\"\n"
                   "- About this computer: \"what time is it\", \"system info\", \"am I online\"\n"
                   "- Switch to dark or light mode, Do Not Disturb, lock the screen, minimize all\n"
                   "- Explain features: \"how do I install a package\", \"keyboard shortcuts\"");
        pip_event(PE_DONE); break;
    case PI_TIP: {
        window_t *f = wm_focused();
        i32 app = -1;
        if (f) for (u32 i = 0; i < PA_COUNT; i++) if (APPMAP[i].app == f->app && f->app != APP_PIP) app = (i32)i;
        kstrcpy(r, pip_tip_for(app, salt())); pip_event(PE_DONE); break;
    }
    case PI_RENAME: kstrcpy(r, "You can rename me in my Home: click my name on the left and type a new one."); pip_event(PE_GREET); break;
    case PI_OPEN_APP: do_open(p, r); break;
    case PI_OPEN_PAGE: do_page(p, r); break;
    case PI_FIND: do_find(p, r); break;
    case PI_INFO: do_info((u32)p->arg, r); break;
    case PI_EXPLAIN: kstrcpy(r, pip_topic_text((u32)p->arg)); pip_event(PE_DONE); break;
    case PI_THEME: {
        app_settings_apply_theme((u32)p->arg);
        kstrcpy(r, p->arg == 0 ? "Dark mode is on." : p->arg == 1 ? "Light mode is on." : "Auto theme is on: light by day, dark at night.");
        pip_event(PE_DONE); say_short("Theme changed!"); break;
    }
    case PI_DND:
        notify_set_dnd(p->arg != 0); settings_set_dnd(p->arg != 0);
        kstrcpy(r, p->arg ? "Do Not Disturb is on. Notifications still collect in the list." : "Do Not Disturb is off.");
        pip_event(PE_DONE); say_short(p->arg ? "Shhh..." : "Notifications back on"); break;
    case PI_LOCK:
        g_lock_request = true;
        kstrcpy(r, "Locking the screen. Sign back in with your password.");
        pip_event(PE_ACTION); break;
    case PI_MIN_ALL: wm_minimize_all(); kstrcpy(r, "All windows minimized."); pip_event(PE_DONE); say_short("Desktop cleared!"); break;
    case PI_CLOSE_ALL: {
        u32 n = 0;
        for (u32 i = 0; i < MAX_WINDOWS; i++) { window_t *w = wm_get_window((int)i); if (w && w->active) n++; }
        if (!n) { kstrcpy(r, "There's nothing open to close."); break; }
        g_confirm = CF_CLOSE_ALL; g_confirm_until = timer_ms() + 30000;
        kstrcpy(r, "That closes "); cat_u(r, n); kstrcat(r, n == 1 ? " window" : " windows"); kstrcat(r, " (including this one), and anything unsaved is lost. Close everything? Say yes or no.");
        pip_event(PE_THINK); break;
    }
    case PI_PAT: pip_event(PE_PAT); kstrcpy(r, "Hehe! That makes me happy. Thank you!"); break;
    case PI_PLAY: pip_desktop_go_play(); kstrcpy(r, b->energy < 30 ? "I'm a bit tired... I'll go for a short wander." : "Yay! Off to explore your desktop. Say \"come back\" when you want me."); pip_event(PE_ACTION); break;
    case PI_COME_BACK: pip_desktop_come_back(); kstrcpy(r, "Back by the dock!"); pip_event(PE_DONE); break;
    case PI_NAP: pip_set_mode(PIP_NAP); kstrcpy(r, "Zzz... a little nap on the dock. Click me when you need me."); pip_event(PE_ACTION); break;
    case PI_DANCE: kstrcpy(r, "Ta-da!"); pip_event(PE_DONE); pip_say("Woohoo!", 3000); break;
    case PI_HIDE:
        b->show = false; pip_mark_dirty();
        kstrcpy(r, "Okay, I'll tuck myself away. I'm still here in my Home, and Settings > Pip brings me back to the desktop.");
        break;
    case PI_DISABLE:
        g_confirm = CF_DISABLE; g_confirm_until = timer_ms() + 30000;
        kstrcpy(r, "Turning me off hides me and stops all my help and tips until you switch me back on in Settings > Pip. Turn me off? Say yes or no.");
        pip_event(PE_THINK); break;
    case PI_TIPS:
        b->tips = p->arg != 0; pip_mark_dirty();
        kstrcpy(r, b->tips ? "Tips are on. I'll only speak up when you've been idle for a bit." : "Okay, no more tips. I'll only talk when you talk to me.");
        pip_event(PE_DONE); break;
    case PI_DELETE:
        kstrcpy(r, "I don't delete things for you -- that's your call, and it can't be undone. Open Files to remove something yourself, or use rm in Terminal.");
        pip_event(PE_OOPS); break;
    case PI_POWER:
        kstrcpy(r, p->arg ? "I can't sign you out myself. Use the power button in Control Center, or lock the screen and switch user there."
                          : "I can't shut down or restart the computer for you. Use the power button in Control Center -- I'll wait here!");
        pip_event(PE_OOPS); break;
    case PI_UNSUPPORTED: kstrcpy(r, pip_nope_text((u32)p->arg)); pip_event(PE_OOPS); say_short("Can't do that yet"); break;
    case PI_YES:
        if (pending) do_confirmed(pending, r);
        else { kstrcpy(r, "Yes to what? I'm not waiting on an answer right now."); pip_event(PE_THINK); }
        g_confirm = CF_NONE;
        break;
    case PI_NO:
        if (pending) { kstrcpy(r, "Okay, I won't. Nothing changed."); pip_event(PE_DONE); }
        else kstrcpy(r, "Okay!");
        g_confirm = CF_NONE;
        break;
    default:
        kstrcpy(r, "Hmm, I don't know how to do that yet, and I'd rather say so than guess. "
                   "I can open apps, jump to Settings, find files, tell you about this computer and explain features. Try \"what can you do?\".");
        pip_event(PE_OOPS); say_short("Hmm...");
        break;
    }
}

/* ---- the conversation loop ------------------------------------------------------ */
void pip_chat_submit(const char *text) {
    if (!pip_brain()->enabled || !text) return;
    pip_parsed_t pr; pip_assist_parse(text, &pr);
    if (pr.kind == PI_EMPTY) return;
    if (g_busy) { /* answer the previous one first, instantly */ g_due_ms = 0; pip_chat_tick(); }
    push(false, text);
    kstrncpy(g_pending_text, text, sizeof g_pending_text - 1);
    g_pending_text[sizeof g_pending_text - 1] = '\0';
    g_busy = true;
    g_due_ms = timer_ms() + 380;
    pip_event(PE_THINK);
}

void pip_chat_tick(void) {
    if (!g_busy || timer_ms() < g_due_ms) return;
    g_busy = false;
    if (!pip_brain()->enabled) return;
    pip_parsed_t pr; pip_assist_parse(g_pending_text, &pr);
    char reply[PIP_MSG_LEN];
    run_intent(&pr, reply);
    if (reply[0]) push(true, reply);
}

i32 pip_app_of_window(const window_t *w) {
    if (!w || w->app == APP_PIP) return -1;
    for (u32 i = 0; i < PA_COUNT; i++) if (APPMAP[i].app == w->app) return (i32)i;
    return -1;
}

void pip_open_home(void) {
    if (!pip_brain()->enabled) return;
    window_t *w = wm_find_app(APP_PIP);
    if (w) { if (w->minimized) wm_minimize(w); wm_focus(w); return; }
    wm_open(APP_PIP, "Pip's Home", 120, 70, (i32)SCREEN_W * 70 / 100, (i32)SCREEN_H * 78 / 100);
}
