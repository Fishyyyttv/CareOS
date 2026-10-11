/* gui/pip/pip_assist.c -- Pip's language layer (pure). See pip_assist.h. */
#include "pip_assist.h"
#include "../../apps/settings_index.h"

#define MAXTOK 24
#define TOKLEN 24
typedef struct { char t[MAXTOK][TOKLEN]; u32 n; } toks_t;

static bool tchar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '/' || c == '-'; }

static void tokenize(const char *s, toks_t *p) {
    p->n = 0;
    u32 len = 0;
    char cur[TOKLEN];
    for (u32 i = 0;; i++) {
        char c = s ? s[i] : 0;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c == '\'' || c == '`') continue;                    /* what's -> whats */
        if (tchar(c)) { if (len + 1 < TOKLEN) cur[len++] = c; continue; }
        if (len) {
            while (len && cur[len - 1] == '.') len--;            /* sentence-ending dot */
            if (len && p->n < MAXTOK) { for (u32 k = 0; k < len; k++) p->t[p->n][k] = cur[k]; p->t[p->n][len] = '\0'; p->n++; }
            len = 0;
        }
        if (!c) break;
    }
}

static bool eqn(const char *a, const char *b, u32 n) { for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return false; return a[n] == '\0'; }

/* Phrase of space-separated tokens, anywhere in the input. */
static bool ph(const toks_t *p, const char *phrase, u32 n) {
    u32 words = 1;
    for (u32 i = 0; i < n; i++) if (phrase[i] == ' ') words++;
    for (u32 s = 0; s + words <= p->n; s++) {
        u32 pos = 0, k = 0; bool ok = true;
        for (; k < words; k++) {
            u32 e = pos; while (e < n && phrase[e] != ' ') e++;
            if (!eqn(p->t[s + k], phrase + pos, e - pos)) { ok = false; break; }
            pos = e + 1;
        }
        if (ok) return true;
    }
    return false;
}
/* "a|b c|d": any alternative (each a word or a phrase). */
static bool any(const toks_t *p, const char *alts) {
    u32 st = 0;
    for (u32 i = 0;; i++) {
        if (alts[i] == '|' || alts[i] == '\0') {
            if (i > st && ph(p, alts + st, i - st)) return true;
            st = i + 1;
            if (!alts[i]) break;
        }
    }
    return false;
}

static void set(pip_parsed_t *o, pip_intent_t k, i32 arg) { o->kind = k; o->arg = arg; }

/* App names people actually say. */
static const struct { const char *alts; u8 app; } APP_ALIASES[] = {
    { "terminal|shell|console|command line|command prompt|cli", PA_TERMINAL },
    { "files|file manager|file browser|explorer|finder|folders", PA_FILES },
    { "notes|notepad|note taking", PA_NOTES },
    { "text editor|code editor|editor|ide", PA_EDITOR },
    { "browser|web browser|internet browser|the web|web", PA_BROWSER },
    { "settings|preferences|control panel|options", PA_SETTINGS },
    { "packages|package manager|package store|software store|app store|store", PA_PKGMGR },
    { "calculator|calc", PA_CALC },
    { "clock|alarm|timer|stopwatch", PA_CLOCK },
    { "paint|drawing|draw", PA_PAINT },
    { "system monitor|task manager|activity monitor|sysmon|process monitor", PA_SYSMON },
    { "network monitor|netmon", PA_NETMON },
    { "users|user manager|accounts app", PA_USERS },
    { "about careos|about this computer", PA_ABOUT },
    { "help app|help center|manual", PA_HELP },
    { "maze", PA_MAZE },
    { "3d|three d|3d demo", PA_3D },
    { "doom", PA_DOOM },
    { "pip home|your home|pips home|home", PA_PIP },
};

static const struct { const char *alts; u8 section; } PAGE_ALIASES[] = {
    { "pip settings|assistant settings|pip options|pip preferences|settings for pip", SET_PIP },
    { "wifi|wi-fi|wireless|dhcp|dns|ethernet|network settings|network", SET_NETWORK },
    { "brightness|resolution|display|pointer|mouse speed|mouse sensitivity|cursor speed", SET_DISPLAY },
    { "theme|wallpaper|background|accent|font|fonts|appearance|transparency|blur|glass|colour|color|colours|colors", SET_APPEARANCE },
    { "sound|volume|audio|mute|beep|speaker|speakers", SET_SOUND },
    { "notification|notifications", SET_NOTIFY },
    { "time zone|timezone|24 hour|24-hour|24h|clock format|date and time", SET_DATETIME },
    { "password|avatar|account|accounts|display name|profile", SET_ACCOUNTS },
    { "storage|disk|disk space|drive space", SET_STORAGE },
};

static const struct { const char *alts; u8 topic; } TOPIC_ALIASES[] = {
    { "package|packages|carepkg|install|installing|uninstall", PT_PACKAGES },
    { "care script|care language|carelang|scripting|script|program|programming|code", PT_CARE },
    { "terminal|command|commands|shell", PT_TERMINAL },
    { "file|files|folder|folders|directory|delete", PT_FILES },
    { "note|notes", PT_NOTES },
    { "browser|website|websites|web page|webpage", PT_BROWSER },
    { "theme|dark mode|light mode|wallpaper", PT_THEME },
    { "lock|locking", PT_LOCK },
    { "shortcut|shortcuts|hotkey|hotkeys|keyboard", PT_SHORTCUTS },
    { "workspace|workspaces|virtual desktop|virtual desktops", PT_WORKSPACES },
    { "launcher|spotlight|super key|search apps", PT_LAUNCHER },
    { "snap|snapping|tile|tiling|split screen", PT_SNAP },
    { "wifi|wi-fi|network|internet|connect", PT_NETWORK },
    { "account|accounts|password|user|users|sign in|log in|login", PT_ACCOUNTS },
    { "pip|you|yourself", PT_PIP },
    { "settings|setting", PT_SETTINGS },
};

static bool is_question(const toks_t *p) {
    return any(p, "how|what|why|explain|teach|tell me about|where|which|when|show me how|help me with|guide me|tutorial|learn|whats the|what is|what are");
}

void pip_assist_parse(const char *input, pip_parsed_t *o) {
    toks_t P; tokenize(input, &P);
    const toks_t *p = &P;
    o->kind = PI_NONE; o->arg = 0; o->flag = 0; o->text[0] = '\0';
    if (!P.n) { set(o, PI_EMPTY, 0); return; }

    /* yes / no: only when that is nearly all they said */
    if (P.n <= 4) {
        static const char *const YES_W[] = { "yes", "y", "yeah", "yep", "yup", "sure", "ok", "okay", "confirm", "do", "it", "go", "ahead", "please", "absolutely", NULL };
        static const char *const NO_W[]  = { "no", "n", "nope", "nah", "cancel", "stop", "never", "mind", "nevermind", "dont", "not", NULL };
        bool all = true, neg = false;
        for (u32 i = 0; i < P.n && all; i++) {
            bool found = false;
            for (u32 k = 0; YES_W[k]; k++) { u32 n = 0; while (YES_W[k][n]) n++; if (eqn(P.t[i], YES_W[k], n)) found = true; }
            for (u32 k = 0; NO_W[k]; k++)  { u32 n = 0; while (NO_W[k][n]) n++;  if (eqn(P.t[i], NO_W[k], n)) { found = true; neg = true; } }
            if (!found) all = false;
        }
        if (all) { set(o, neg ? PI_NO : PI_YES, 0); return; }
    }
    bool q = is_question(p) && !any(p, "how many|how much");

    /* things Pip will not do for you */
    if (!q && any(p, "delete|erase|wipe|rm|trash|format|destroy")) { set(o, PI_DELETE, 0); return; }
    if (any(p, "shutdown|shut down|power off|turn off the computer|turn off computer|restart|reboot|log out|logout|sign out|switch user")) {
        set(o, PI_POWER, any(p, "log out|logout|sign out|switch user") ? 1 : 0); return;
    }
    if (any(p, "weather|forecast|temperature outside")) { set(o, PI_UNSUPPORTED, PN_WEATHER); return; }
    if (any(p, "battery|charge level|charging")) { set(o, PI_UNSUPPORTED, PN_BATTERY); return; }
    if (any(p, "play music|play a song|play song|play video|music|spotify|youtube")) { set(o, PI_UNSUPPORTED, PN_MEDIA); return; }

    /* Pip himself */
    if (any(p, "tips off|no tips|stop tips|stop the tips|disable tips|turn off tips|dont give tips|no suggestions|stop suggestions")) { set(o, PI_TIPS, 0); return; }
    if (any(p, "tips on|enable tips|turn on tips|give me tips|more tips|start tips")) { set(o, PI_TIPS, 1); return; }
    if (any(p, "turn off pip|disable pip|switch off pip|shut down pip|turn yourself off|disable yourself|quit pip|turn pip off|stop being here|turn you off")) { set(o, PI_DISABLE, 0); return; }
    if (any(p, "hide|go away|leave me alone|get off my screen|disappear|hide yourself|dismiss") && !any(p, "hide all|hide everything|hide windows")) { set(o, PI_HIDE, 0); return; }
    if (any(p, "rename|change your name|new name|call you")) { set(o, PI_RENAME, 0); return; }
    if (any(p, "who are you|what are you|your name|introduce yourself|about you|who is pip|what is pip")) { set(o, PI_ABOUT_PIP, 0); return; }
    if (any(p, "what can you do|what do you do|what can i ask|what can i say|commands you|your commands|capabilities|abilities|what can pip do")
        || (P.n == 1 && any(p, "help"))) { set(o, PI_CAPS, 0); return; }
    if (any(p, "how are you|how r you|how are u|hows it going|how you doing|whats up|how do you feel|are you ok")) { set(o, PI_HOW_ARE_YOU, 0); return; }
    if (any(p, "tour|get started|getting started|new here|im new|i am new|show me around|walk me through|first time|beginner")) { set(o, PI_EXPLAIN, PT_TOUR); return; }
    if (any(p, "give me a tip|a tip|tip|suggest|suggestion|any ideas|surprise me")) { set(o, PI_TIP, 0); return; }
    if (any(p, "pat you|pat pip|pet you|pet pip|headpat|good boy|good girl|good pip|good job|well done|you are cute|youre cute|cute|i love you|love you|hug")) { set(o, PI_PAT, 0); return; }
    if (any(p, "come back|come here|come home|stay here|stop roaming|stop playing|sit down|go home")) { set(o, PI_COME_BACK, 0); return; }
    if (any(p, "go play|play around|roam|go for a walk|go explore|run around|have fun|play") && !any(p, "doom|maze|3d")) { set(o, PI_PLAY, 0); return; }
    if (any(p, "nap|take a nap|go to sleep|sleep|go to bed|bedtime|rest|good night|goodnight|tuck in")) { set(o, PI_NAP, 0); return; }
    if (any(p, "dance|celebrate|do a trick|jump|party|cheer")) { set(o, PI_DANCE, 0); return; }
    if (any(p, "thanks|thank you|thank|thx|ty|cheers|appreciate")) { set(o, PI_THANKS, 0); return; }
    if (any(p, "bye|goodbye|see you|see ya|later|cya|gotta go")) { set(o, PI_BYE, 0); return; }

    /* system actions */
    if (any(p, "dnd|do not disturb|dont disturb|quiet mode|focus mode|silence notifications")) {
        set(o, PI_DND, any(p, "off|disable|stop|end|exit|leave") ? 0 : 1); return;
    }
    if (!q && (any(p, "lock") && !any(p, "unlock|block|clock|lock in"))) { set(o, PI_LOCK, 0); return; }
    if (!q && (any(p, "show desktop|clear desktop|minimize all|minimise all|minimize everything|minimise everything|hide all windows|hide everything|minimize windows|minimise windows|minimize all windows|minimise all windows"))) { set(o, PI_MIN_ALL, 0); return; }
    if (!q && any(p, "close") && any(p, "all|everything|every")) { set(o, PI_CLOSE_ALL, 0); return; }
    if (any(p, "dark mode|light mode|dark theme|light theme|go dark|go light|auto theme|automatic theme|night mode|day mode")
        || ((any(p, "dark|light|auto|automatic")) && any(p, "mode|theme|switch|set|make|use|turn|change|enable"))) {
        if (!(q && any(p, "how|where|what|which"))) {
            set(o, PI_THEME, any(p, "light|day") ? 1 : any(p, "auto|automatic") ? 2 : 0);
            return;
        }
    }

    /* "where do I change X": take them there (the how-to explanations are for how/what/why) */
    if (any(p, "where") && !any(p, "where is|wheres|where are") && !any(p, "how|why|what|explain|teach|tutorial|learn")) {
        for (u32 i = 0; i < sizeof PAGE_ALIASES / sizeof PAGE_ALIASES[0]; i++)
            if (any(p, PAGE_ALIASES[i].alts)) { set(o, PI_OPEN_PAGE, PAGE_ALIASES[i].section); return; }
    }

    /* how-to questions about a feature */
    if (q) {
        for (u32 i = 0; i < sizeof TOPIC_ALIASES / sizeof TOPIC_ALIASES[0]; i++)
            if (any(p, TOPIC_ALIASES[i].alts)) {
                /* "how do I open the terminal" is really a request */
                if (!any(p, "open|launch|start|run") || TOPIC_ALIASES[i].topic == PT_PACKAGES) { set(o, PI_EXPLAIN, TOPIC_ALIASES[i].topic); return; }
            }
    }

    /* facts about this machine */
    if (any(p, "my ip|ip address|am i online|am i connected|are we online|are we connected|internet connection|connected to|online|offline|network status|link status|whats my ip")) { set(o, PI_INFO, PINF_NETWORK); return; }
    if (any(p, "how many windows|what windows|windows are open|windows do i have|apps are open|windows open|open windows|whats open|what is open|running apps|which apps|what apps are open|open apps|whats running|what is running")) { set(o, PI_INFO, PINF_WINDOWS); return; }
    if (any(p, "installed packages|how many packages|packages installed|packages do i have|what packages")) { set(o, PI_INFO, PINF_PACKAGES); return; }
    if (any(p, "what time|the time|current time|time is it|time now|tell me the time|clock time") && !any(p, "zone|timezone|format")) { set(o, PI_INFO, PINF_TIME); return; }
    if (any(p, "the date|what day|todays date|today|current date|what date|day is it")) { set(o, PI_INFO, PINF_DATE); return; }
    if (any(p, "memory|ram|free memory|mem usage|how much memory")) { set(o, PI_INFO, PINF_MEMORY); return; }
    if (any(p, "uptime|how long have you been|been up|up for|running for|since boot")) { set(o, PI_INFO, PINF_UPTIME); return; }
    if (any(p, "who am i|whoami|my name|my username|logged in as|current user|which user|who is logged in")) { set(o, PI_INFO, PINF_USER); return; }
    if (any(p, "system info|sysinfo|system information|specs|version|cpu|processor|about this|this computer|which os|what os")) { set(o, PI_INFO, PINF_SYSTEM); return; }

    /* settings pages */
    bool has_set = any(p, "settings|setting|preferences|options|change|set|adjust|configure|customize|customise|where|open|go to|show|take me");
    if (has_set) {
        for (u32 i = 0; i < sizeof PAGE_ALIASES / sizeof PAGE_ALIASES[0]; i++)
            if (any(p, PAGE_ALIASES[i].alts)) {
                /* "open network monitor" is the app, not the page */
                if (PAGE_ALIASES[i].section == SET_NETWORK && any(p, "network monitor|netmon")) break;
                if (PAGE_ALIASES[i].section == SET_APPEARANCE && any(p, "font") && any(p, "editor")) break;
                set(o, PI_OPEN_PAGE, PAGE_ALIASES[i].section); return;
            }
    }

    /* find a file */
    if (any(p, "find|locate|search|look for|looking for|where is|wheres|where are")) {
        static const char *const FILLER[] = { "find", "locate", "search", "look", "looking", "for", "where", "is", "are", "wheres", "my", "the", "a", "an", "me",
            "called", "named", "file", "files", "folder", "folders", "directory", "please", "can", "you", "could", "on", "in", "computer", "pc", "i", "want", "to", "need", "do", "have", "s", NULL };
        bool web = any(p, "web|google|online|internet|the web|bing|duckduckgo");
        for (u32 i = 0; i < P.n; i++) {
            bool filler = false;
            for (u32 f = 0; FILLER[f]; f++) { u32 n = 0; while (FILLER[f][n]) n++; if (eqn(P.t[i], FILLER[f], n)) { filler = true; break; } }
            if (filler) continue;
            u32 n = 0; while (P.t[i][n] && n < sizeof o->text - 1) { o->text[n] = P.t[i][n]; n++; }
            o->text[n] = '\0';
            break;
        }
        if (web) { set(o, PI_UNSUPPORTED, PN_WEB_SEARCH); return; }
        /* "where is settings" is a question about an app, not a file */
        bool is_app = false;
        for (u32 i = 0; i < sizeof APP_ALIASES / sizeof APP_ALIASES[0]; i++)
            if (any(p, APP_ALIASES[i].alts)) is_app = true;
        if (!is_app) { set(o, PI_FIND, 0); return; }
        o->text[0] = '\0';
    }

    /* open an app */
    bool verb = any(p, "open|launch|start|run|show|go to|switch to|take me to|bring up|fire up|load|use|need|want|visit");
    for (u32 i = 0; i < sizeof APP_ALIASES / sizeof APP_ALIASES[0]; i++) {
        if (!any(p, APP_ALIASES[i].alts)) continue;
        u32 app = APP_ALIASES[i].app;
        if (!verb && P.n > 3 && !q) continue;        /* a long sentence that merely mentions an app */
        if (app == PA_BROWSER && any(p, "search the web|search web|google")) { set(o, PI_UNSUPPORTED, PN_WEB_SEARCH); return; }
        set(o, PI_OPEN_APP, (i32)app); return;
    }
    if (any(p, "search|google")) { set(o, PI_UNSUPPORTED, PN_WEB_SEARCH); return; }

    /* greetings last, so "hey open settings" is still an action */
    if (any(p, "hello|hi|hey|hiya|howdy|yo|hola|sup|good morning|good afternoon|good evening|greetings")) { set(o, PI_GREET, 0); return; }
    if (any(p, "help|assist|support")) { set(o, PI_CAPS, 0); return; }
    set(o, PI_NONE, 0);
}

/* ---------------------------------------------------------------------------
 * Words
 * ------------------------------------------------------------------------- */
static const char *const APP_NAMES[PA_COUNT] = {
    "Terminal", "Notes", "Files", "System Monitor", "Calculator", "About CareOS", "Help", "Browser",
    "Settings", "Packages", "Editor", "Paint", "Clock", "Network Monitor", "Users", "Maze",
    "3D Demo", "DOOM", "Pip's Home"
};
const char *pip_app_name(u32 app) { return app < PA_COUNT ? APP_NAMES[app] : "that app"; }

static const char *const TOPIC_TITLES[PT_COUNT] = {
    "Getting started", "Packages", "Terminal", "The Care language", "Files", "Notes", "Browser", "Themes",
    "Locking", "Shortcuts", "Workspaces", "The launcher", "Window snapping", "Network", "Accounts", "About Pip",
    "Settings"
};
const char *pip_topic_title(u32 t) { return t < PT_COUNT ? TOPIC_TITLES[t] : ""; }

static const char *const TOPIC_TEXT[PT_COUNT] = {
    /* tour */ "Welcome to CareOS! Tap Super to open the launcher and find any app, or use the dock at the bottom. "
               "Files shows your folders, Notes is for writing, Terminal runs commands, and Settings changes how things look. "
               "Ask me to open anything, or ask how a feature works.",
    /* packages */ "Open Packages from the dock or the launcher. It lists the .care packages on this machine; Get installs one, Update refreshes it, and Remove (click twice to confirm) takes it off. "
                   "There is no online catalog yet. In Terminal the same thing is: carepkg list, carepkg install <path.care>, carepkg remove <name>, carepkg info <name>.",
    /* terminal */ "Terminal runs commands. Files: ls cd pwd cat mkdir rm touch echo. System: uname date free uptime sysinfo ps whoami. "
                   "Network: ifconfig ping curl wifi network. CareOS: carepkg dmesg settings perf. Type help for the full list; Tab completes commands and paths.",
    /* care */ "Care is CareOS's own little language. Open Editor, write a script, and press Run; the output shows underneath. "
               "In Terminal, care <file> runs one too. Scripts can talk to me: pip_say(\"hi\") puts words in my speech bubble and pip_mood(\"happy\") changes my face.",
    /* files */ "Files browses your folders. Your home folder is where your notes and settings live. In Terminal, ls lists a folder, cd moves, and cat shows a file. "
                "I never delete things for you; rm in Terminal does it, and it cannot be undone.",
    /* notes */ "Notes saves your notes as files in a Notes folder in your home directory, and shows simple Markdown (headings, lists, bold) nicely. Ask me to open it any time.",
    /* browser */ "Browser opens web pages over the network, so the link has to be up. Ask me \"am I online?\" to check, or open Network in Settings to renew the connection.",
    /* theme */ "Open Settings > Appearance to pick dark, light or auto, a wallpaper, an accent colour and a font. Or just tell me \"dark mode\" or \"light mode\" and I'll switch it.",
    /* lock */ "Press Super+L to lock the screen, or use the lock tile in Control Center. Or say \"lock the screen\" and I'll do it. You sign back in with your password.",
    /* shortcuts */ "Super (tap): launcher. Super+L: lock. Ctrl+1 to Ctrl+4: switch workspace. Ctrl+Alt+H / L / K / J: snap a window left / right / up / down, Ctrl+Alt+M: fill the screen. "
                    "Ctrl+Alt+P: performance HUD. In Terminal, Ctrl+T cycles colours.",
    /* workspaces */ "CareOS has four workspaces. Press Ctrl+1, Ctrl+2, Ctrl+3 or Ctrl+4 to jump between them; each keeps its own windows.",
    /* launcher */ "Tap the Super key to open the launcher, type to search, press Enter to open the first result. The dock at the bottom also has your main apps.",
    /* snap */ "Focus a window, then press Ctrl+Alt+H to snap it left, Ctrl+Alt+L right, Ctrl+Alt+K top, Ctrl+Alt+J bottom, or Ctrl+Alt+M to fill the screen.",
    /* network */ "Settings > Network shows your link, IP address and gateway, and has Run diagnostics and Renew DHCP buttons. Ask me \"am I online?\" for a quick check.",
    /* accounts */ "Settings > Accounts lets you change your display name, password and avatar colour. Lock the screen to switch users; guests get a session that is not saved.",
    /* pip */ "I'm Pip! I live on your desktop and I can open apps, jump to a Settings page, find files, tell you about this computer and explain how CareOS works. "
              "Right-click me for a menu, pat me for happiness, and turn me on or off in Settings > Pip.",
    /* settings */ "Settings has Network, Display, Appearance, Sound, Notifications, Date & Time, Accounts, Storage, Pip and About. Tell me what you want to change and I'll open the right page.",
};
const char *pip_topic_text(u32 t) { return t < PT_COUNT ? TOPIC_TEXT[t] : ""; }

static const char *const NOPE_TEXT[PN_COUNT] = {
    "CareOS has no weather source yet, so I won't invent one. A browser page might help.",
    "This machine doesn't report a battery, so I have nothing real to tell you.",
    "I can't search the web for you yet. I can open Browser so you can go to a page yourself.",
    "CareOS has no media player or sound card driver yet, so I can't play anything. Sorry!",
};
const char *pip_nope_text(u32 n) { return n < PN_COUNT ? NOPE_TEXT[n] : ""; }

static const struct { const char *label, *text; } QUICK[] = {
    { "What can you do?", "what can you do" },
    { "Open Settings",    "open settings" },
    { "Open Files",       "open files" },
    { "Open Terminal",    "open terminal" },
    { "System info",      "system info" },
    { "Am I online?",     "am i online" },
    { "Dark mode",        "dark mode" },
    { "How do I install a package?", "how do i install a package" },
};
u32 pip_quick_count(void) { return sizeof QUICK / sizeof QUICK[0]; }
const char *pip_quick_label(u32 i) { return i < pip_quick_count() ? QUICK[i].label : ""; }
const char *pip_quick_text(u32 i) { return i < pip_quick_count() ? QUICK[i].text : ""; }

static const char *const DESK_TIPS[] = {
    "Tip: tap Super to open the launcher and search for any app.",
    "Tip: Ctrl+1 to Ctrl+4 switch workspaces.",
    "Tip: Super+L locks the screen.",
    "Tip: click me and ask to open an app. Try \"open notes\".",
    "Tip: Settings > Appearance has dark, light and auto themes.",
};
static const char *const TERM_TIPS[] = {
    "Tip: Tab completes commands and paths.",
    "Tip: type help to see every Terminal command.",
    "Tip: carepkg list shows your installed packages.",
};
static const char *const FILES_TIPS[] = { "Tip: ask me \"find notes\" and I'll search your folders." };
static const char *const EDIT_TIPS[] = { "Tip: press Run, and your script can call pip_say(\"hi\") to talk to me." };
static const char *const SET_TIPS[] = { "Tip: Settings > Pip has switches for me, if I get in the way." };

const char *pip_tip_for(i32 app, u32 salt) {
    switch (app) {
    case PA_TERMINAL: return TERM_TIPS[salt % 3];
    case PA_FILES:    return FILES_TIPS[0];
    case PA_EDITOR:   return EDIT_TIPS[0];
    case PA_SETTINGS: return SET_TIPS[0];
    default:          return DESK_TIPS[salt % 5];
    }
}
