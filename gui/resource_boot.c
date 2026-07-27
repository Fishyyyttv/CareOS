/* =============================================================================
 * CareOS gui/resource_boot.c  --  publish the built-in asset tree
 *
 * /system is an in-memory VFS that is rebuilt on every boot (see the note in
 * include/appdb.h), so system icons cannot simply "be on disk" the way they are
 * on a hosted OS. They are linked into the kernel image instead, exactly like
 * DOOM1.WAD: tools/gen-icons.py bakes a .cra archive and the Makefile turns it
 * into an object with objcopy.
 *
 * The archive is MOUNTED, not unpacked. res_image() consults it after the VFS,
 * so /system/icons/48/browser.cri resolves without any fs_node_t existing.
 * Unpacking was the first design and it was wrong: the VFS is one fixed pool of
 * 128 nodes for the entire OS, and creating a node per icon ate every free slot
 * at boot -- truncating the theme to whatever fitted and starving package
 * installs of the nodes they need. Mounting costs nothing and cannot starve
 * anything.
 *
 * The directories below ARE created, because they are the override point: a
 * file dropped at /system/icons/browser.bmp wins over the baked artwork, and
 * you cannot drop a file into a directory that does not exist.
 *
 * A missing or empty archive is not an error. The Makefile stubs one out so a
 * fresh checkout links, and every drawing path falls back to the vector glyphs
 * in gfx_draw_icon(), which is exactly the desktop CareOS had before.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"

/* Emitted by `objcopy -I binary` from assets/careos-icons.cra. The symbol name
 * is derived from the path, so moving the file means renaming these. */
extern u8 _binary_assets_careos_icons_cra_start[];
extern u8 _binary_assets_careos_icons_cra_end[];

void resources_init(void) {
    res_cache_init();

    /* Directories exist whether or not the archive does, so a user can drop a
     * loose browser.bmp into /system/icons on a build with no baked theme. */
    fs_node_t *system_dir = vfs_resolve_path("/system");
    if (!system_dir) system_dir = vfs_mkdir(vfs_root(), "system");
    if (system_dir) {
        if (!vfs_find(system_dir, "icons"))      vfs_mkdir(system_dir, "icons");
        if (!vfs_find(system_dir, "wallpapers")) vfs_mkdir(system_dir, "wallpapers");
        if (!vfs_find(system_dir, "fonts"))      vfs_mkdir(system_dir, "fonts");
    }

    u32 len = (u32)(uintptr_t)(_binary_assets_careos_icons_cra_end -
                               _binary_assets_careos_icons_cra_start);
    if (len <= CRA_HEADER_BYTES) {
        serial_write("[res] no baked icon theme; using vector glyphs\n");
        return;
    }

    /* Mounted at /system, not /system/icons: entry names carry their own
     * subtree ("icons/48/browser.cri", "wallpapers/default.cri"), so one
     * archive and one call cover the whole resource directory. */
    if (res_mount_archive(_binary_assets_careos_icons_cra_start, len, "/system") < 0) {
        serial_write("[res] icon archive rejected; using vector glyphs\n");
        return;
    }

    /* Resolve one icon for real, through the same path the launcher uses.
     * Mounting an archive only proves the header parsed; this proves a name
     * reaches a decoded image. Without it, a theme that mounts but resolves
     * nothing looks identical at the console to no theme at all -- and since
     * the fallback is a working vector glyph, nothing on screen says so
     * either. One line in the log is the difference between a five-minute
     * diagnosis and an afternoon of guessing. */
    image_t *probe = icon_lookup("terminal", 48);
    if (probe) {
        char nb[12];
        kutoa(probe->width, nb, 10);
        serial_write("[res] icon theme live (terminal@");
        serial_write(nb);
        serial_write(" decoded)\n");
    } else {
        serial_write("[res] WARNING: archive mounted but no icon resolved; "
                     "check the entry names against gui/icon.c\n");
    }
}
