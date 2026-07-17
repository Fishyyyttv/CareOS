/* CareOS v9 -- kernel/rc_care.c -- system & per-user startup scripts (rc.care) */
#include "kernel.h"

#define RC_CARE_MAX_SIZE 8192

static const char *RC_CARE_DEFAULT_SCRIPT =
    "# CareOS startup script -- this runs every time you log in.\n"
    "# Uncomment any line below to try it out.\n"
    "\n"
    "# sys_launch(\"notes\");\n"
    "# sys_set_theme(1);\n"
    "# if (sys_first_run()) {\n"
    "#     sys_alert(\"Welcome to CareOS!\");\n"
    "# }\n";

static void rc_care_seed_default(fs_node_t *dir, const char *filename) {
    fs_node_t *f = vfs_mkfile(dir, filename);
    if (f) vfs_write(f, RC_CARE_DEFAULT_SCRIPT, (u32)kstrlen(RC_CARE_DEFAULT_SCRIPT));
}

static bool rc_care_read_file(const char *path, char *buf, u32 buf_max) {
    fs_node_t *f = vfs_resolve_path(path);
    if (!f) return false;
    int n = vfs_read(f, buf, buf_max - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    return true;
}

static void rc_care_run_one(const char *path, const char *which) {
    char buf[RC_CARE_MAX_SIZE];
    if (!rc_care_read_file(path, buf, sizeof(buf))) return;
    serial_write("  [rc_care] running ");
    serial_write(which);
    serial_write(" script\n");
    if (care_lang_exec(buf, (u32)kstrlen(buf)) != 0) {
        serial_write("  [rc_care] ");
        serial_write(which);
        serial_write(" script error\n");
    }
}

void rc_care_run_startup(void) {
    /* system-wide: /etc/rc.care */
    fs_node_t *etc = vfs_find(vfs_root(), "etc");
    if (!etc) etc = vfs_mkdir(vfs_root(), "etc");
    if (etc) {
        if (!vfs_find(etc, "rc.care")) {
            rc_care_seed_default(etc, "rc.care");
            serial_write("  [rc_care] seeded /etc/rc.care\n");
        } else {
            rc_care_run_one("/etc/rc.care", "system");
        }
    }

    /* per-user: /home/<user>/rc.care */
    char home_path[48];
    ksprintf(home_path, "/home/%s", user_current_name());
    fs_node_t *home = vfs_resolve_path(home_path);
    if (home) {
        if (!vfs_find(home, "rc.care")) {
            rc_care_seed_default(home, "rc.care");
            serial_write("  [rc_care] seeded user rc.care\n");
        } else {
            char user_path[64];
            ksprintf(user_path, "/home/%s/rc.care", user_current_name());
            rc_care_run_one(user_path, "user");
        }
    }
}
