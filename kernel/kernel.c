/* =============================================================================
 * CareOS v9 -- kernel/kernel.c
 * Licensed under the GNU General Public License v3 (GPLv3).
 * Copyright (C) 2026 [Your Name]
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

#define MB2_MAGIC 0x36D76289u

#define MB2_TAG_END         0u
#define MB2_TAG_FRAMEBUFFER 8u

typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    u32 type;
    u32 size;
    u64 fb_addr;
    u32 fb_pitch;
    u32 fb_width;
    u32 fb_height;
    u8  fb_bpp;
    u8  fb_type;
    u16 reserved;
    u8  red_field_pos;
    u8  red_mask_size;
    u8  green_field_pos;
    u8  green_mask_size;
    u8  blue_field_pos;
    u8  blue_mask_size;
} __attribute__((packed)) mb2_tag_fb_t;

static void slog_u32(const char *label, u32 v){
    serial_write(label);
    char b[12];
    kutoa(v, b, 16);
    serial_write("0x");
    serial_write(b);
    serial_write("\n");
}

static void slog_stage(int n, const char *desc){
    serial_write("\n[STAGE ");
    char b[4];
    kutoa((u32)n, b, 10);
    serial_write(b);
    serial_write("] ");
    serial_write(desc);
    serial_write("\n");
}

static void slog_ok(const char *subsys){
    serial_write("  [OK] ");
    serial_write(subsys);
    serial_write("\n");
}

void kernel_main(u64 magic, u64 mbi_addr){
    /* Stage 1: core hardware */
    slog_stage(1, "Hardware init");
    pmm_init();      slog_ok("PMM");
    gdt_init();      slog_ok("GDT");
    terminal_init(); slog_ok("VGA terminal");
    serial_init();   slog_ok("Serial (COM1)");
    idt_init();      slog_ok("IDT");

    if (magic != MB2_MAGIC) {
        serial_write("[FATAL] Bad Multiboot2 magic: 0x");
        char mb[16]; kutoa((u32)magic, mb, 16); serial_write(mb);
        serial_write("\n");
        terminal_write_colored("FATAL: Bad MB2 magic. Boot with Multiboot2 loader.\n",
            VGA_ENTRY_COLOR(VGA_RED, VGA_BLACK));
        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
    }

    /* Stage 2: core kernel services */
    slog_stage(2, "Core services");
    timer_init(PIT_HZ);  slog_ok("PIT timer");
    keyboard_init();     slog_ok("PS/2 keyboard");
    vfs_init();          slog_ok("VFS");

    /* Ensure standard VFS directories exist */
    {
        fs_node_t *usr = vfs_find(vfs_root(), "usr");
        if (!usr) usr = vfs_mkdir(vfs_root(), "usr");
        if (usr) { fs_node_t *b = vfs_find(usr, "bin"); if (!b) vfs_mkdir(usr, "bin"); }
        fs_node_t *var = vfs_find(vfs_root(), "var");
        if (!var) var = vfs_mkdir(vfs_root(), "var");
        if (var) { fs_node_t *pkg = vfs_find(var, "pkg"); if (!pkg) vfs_mkdir(var, "pkg"); }

        /* /System directory with info and Care language samples */
        fs_node_t *sys = vfs_find(vfs_root(), "System");
        if (!sys) sys = vfs_mkdir(vfs_root(), "System");
        if (sys) {
            fs_node_t *ver = vfs_mkfile(sys, "version");
            if (ver) {
                const char *s = "CareOS v9\nBuild: 2026\nKernel: 9.0.0\nArch: x86-64\n";
                vfs_write(ver, s, (u32)kstrlen(s));
            }
            fs_node_t *inf = vfs_mkfile(sys, "info");
            if (inf) {
                const char *s =
                    "CareOS is a lightweight custom operating system.\n"
                    "Built from scratch in C and x86-64 assembly.\n"
                    "Features: GUI, networking, VFS, package manager.\n"
                    "Care Language (.cl) is the native scripting language.\n";
                vfs_write(inf, s, (u32)kstrlen(s));
            }
            fs_node_t *samp = vfs_mkdir(sys, "samples");
            if (samp) {
                fs_node_t *h = vfs_mkfile(samp, "hello.cl");
                if (h) {
                    const char *s =
                        "# Hello World in Care language\n"
                        "var name = \"CareOS\";\n"
                        "print \"Hello from \" + name + \"!\";\n"
                        "var x = 42;\n"
                        "print \"The answer is \" + x;\n";
                    vfs_write(h, s, (u32)kstrlen(s));
                }
                fs_node_t *ct = vfs_mkfile(samp, "counter.cl");
                if (ct) {
                    const char *s =
                        "# Counter example\n"
                        "var i = 1;\n"
                        "while (i <= 5) {\n"
                        "    print i;\n"
                        "    i = i + 1;\n"
                        "}\n"
                        "print \"Done!\";\n";
                    vfs_write(ct, s, (u32)kstrlen(s));
                }
                fs_node_t *fb = vfs_mkfile(samp, "grades.cl");
                if (fb) {
                    const char *s =
                        "# Grade checker and sum calculator\n"
                        "var score = 85;\n"
                        "print \"Score: \" + score;\n"
                        "if (score >= 90) {\n"
                        "    print \"Grade: A\";\n"
                        "} else {\n"
                        "    if (score >= 80) {\n"
                        "        print \"Grade: B\";\n"
                        "    } else {\n"
                        "        if (score >= 70) {\n"
                        "            print \"Grade: C\";\n"
                        "        } else {\n"
                        "            print \"Grade: F\";\n"
                        "        }\n"
                        "    }\n"
                        "}\n"
                        "var i = 1;\n"
                        "var sum = 0;\n"
                        "while (i <= 10) {\n"
                        "    sum = sum + i;\n"
                        "    i = i + 1;\n"
                        "}\n"
                        "print \"Sum 1..10 = \" + sum;\n";
                    vfs_write(fb, s, (u32)kstrlen(s));
                }
            }
        }
    }
    paging_init();       slog_ok("Paging");
    syscall_init();      slog_ok("Syscalls");

    /* ── Ring-3 smoke test: embed ring3_exit ELF and launch it ────────────────── */
    {
        extern u8 _binary_tests_ring3_exit_start[];
        extern u8 _binary_tests_ring3_exit_end[];
        u32 elf_size = (u32)(uintptr_t)(_binary_tests_ring3_exit_end - _binary_tests_ring3_exit_start);
        serial_write("[smoke] ring3_exit ELF size = ");
        char _sb[12]; kutoa(elf_size, _sb, 10); serial_write(_sb); serial_write("\n");

        fs_node_t *bin_dir = vfs_mkdir(vfs_root(), "bin");
        if (!bin_dir) bin_dir = vfs_find(vfs_root(), "bin");
        if (bin_dir && elf_size > 0 && elf_size <= FS_FILE_DATA_MAX) {
            fs_node_t *n = vfs_mkfile(bin_dir, "ring3_exit");
            if (n) {
                kmemcpy(n->data, _binary_tests_ring3_exit_start, elf_size);
                n->size = elf_size;
                int tid = elf_load_user(n, "ring3_exit", -1);
                serial_write("[smoke] elf_load_user returned ");
                kitoa(tid, _sb, 10); serial_write(_sb); serial_write("\n");
            }
        } else if (elf_size > FS_FILE_DATA_MAX) {
            serial_write("[smoke] ELF too large for VFS node data field\n");
        }
    }

    /* Stage 3: devices, persistence, userland services */
    slog_stage(3, "Device and persistence init");
    rtc_init();          slog_ok("RTC");
    pci_init();          slog_ok("PCI");
    ata_init();          slog_ok("ATA/IDE");
    if (ext2_mount() == 0) {
        slog_ok("ext2");
    } else {
        serial_write("[kernel] WARNING: ext2 mount failed - run make format-disk\n");
    }

    /* TEMP: ext2 read test -- remove after verification */
    {
        u32 root_ino = ext2_path_to_inode("/");
        serial_write(root_ino == 2 ? "[ext2 test] root OK\n" : "[ext2 test] root FAILED\n");
    }

    settings_init();     slog_ok("Settings");
    vfs_storage_online();slog_ok("Home persistence");
    users_init();        slog_ok("Users");

    carepkg_init();      slog_ok("CarePackage manager");
    scheduler_init();    slog_ok("Scheduler");

    e1000_init();
    net_init();          slog_ok("Networking");

    /* Stage 4: framebuffer detection */
    slog_stage(4, "Framebuffer detection");

    u32 fb_w = 0, fb_h = 0, fb_pitch = 0, fb_bpp = 0;
    u64 fb_addr = 0;
    u8 fb_r_pos = 16, fb_g_pos = 8, fb_b_pos = 0;

    slog_u32("  MB2 magic  = ", (u32)magic);
    slog_u32("  MB2 mbi    = ", (u32)mbi_addr);

    if (mbi_addr) {
        u32 *hdr = (u32*)(uintptr_t)mbi_addr;
        u32 total = hdr[0];
        slog_u32("  MB2 total  = ", total);

        u32 offset = 8;
        while (offset < total) {
            mb2_tag_t *tag = (mb2_tag_t*)(mbi_addr + offset);
            if (tag->type == MB2_TAG_END) break;

            if (tag->type == MB2_TAG_FRAMEBUFFER) {
                mb2_tag_fb_t *fb = (mb2_tag_fb_t*)tag;
                fb_addr  = fb->fb_addr;
                fb_pitch = fb->fb_pitch;
                fb_w     = fb->fb_width;
                fb_h     = fb->fb_height;
                fb_bpp   = fb->fb_bpp;

                if (fb->fb_type == 1) {
                    fb_r_pos = fb->red_field_pos;
                    fb_g_pos = fb->green_field_pos;
                    fb_b_pos = fb->blue_field_pos;
                    serial_write("  [FB] Direct-color mask shifts parsed\n");
                }

                slog_u32("  FB addr    = ", (u32)fb_addr);
                slog_u32("  FB width   = ", fb_w);
                slog_u32("  FB height  = ", fb_h);
                slog_u32("  FB pitch   = ", fb_pitch);
                slog_u32("  FB bpp     = ", fb_bpp);
                slog_ok("MB2 framebuffer tag found");
            }

            offset += (tag->size + 7u) & ~7u;
        }
    } else {
        serial_write("  [WARN] No MBI pointer, probing VBE\n");
    }

    /* Fallback probe for QEMU/Bochs VBE */
    if (!fb_addr || fb_w == 0 || (fb_bpp != 32 && fb_bpp != 24)) {
        serial_write("  [INFO] No valid MB2 FB, probing VBE ports\n");

        #define VBE_DISPI_IOPORT_INDEX  0x01CE
        #define VBE_DISPI_IOPORT_DATA   0x01CF
        #define VBE_DISPI_INDEX_XRES    0x01
        #define VBE_DISPI_INDEX_YRES    0x02
        #define VBE_DISPI_INDEX_BPP     0x03
        #define VBE_DISPI_INDEX_ENABLE  0x04
        #define VBE_DISPI_ENABLED       0x01
        #define VBE_DISPI_LFB_ENABLED   0x40

        typedef struct {
            u16 w, h;
        } vbe_mode_t;

        static const vbe_mode_t modes[] = {
            {1920, 1080},
            {1600,  900},
            {1280,  720},
            {1024,  768},
        };

        pci_device_t *disp = pci_find_class(0x03, 0x00);
        if (disp) {
            u32 bar0 = disp->bar[0] & ~0xFu;
            slog_u32("  VGA BAR0   = ", bar0);
            if (bar0 >= 0x100000u) {
                for (u32 i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
                    outw(VBE_DISPI_IOPORT_INDEX, 0x00);
                    outw(VBE_DISPI_IOPORT_DATA,  0xB0C4);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
                    outw(VBE_DISPI_IOPORT_DATA,  modes[i].w);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
                    outw(VBE_DISPI_IOPORT_DATA,  modes[i].h);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
                    outw(VBE_DISPI_IOPORT_DATA,  32);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
                    outw(VBE_DISPI_IOPORT_DATA,  VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
                    u16 got_w = inw(VBE_DISPI_IOPORT_DATA);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
                    u16 got_h = inw(VBE_DISPI_IOPORT_DATA);
                    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
                    u16 got_bpp = inw(VBE_DISPI_IOPORT_DATA);

                    if (got_w == modes[i].w && got_h == modes[i].h && got_bpp == 32) {
                        fb_addr  = (u64)(uintptr_t)bar0;
                        fb_w     = got_w;
                        fb_h     = got_h;
                        fb_bpp   = 32;
                        fb_pitch = (u32)got_w * 4u;
                        paging_map_mmio(bar0, (u32)got_h * fb_pitch + 0x400000u);
                        serial_write("  [FB] VBE fallback selected\n");
                        slog_u32("  FB width   = ", fb_w);
                        slog_u32("  FB height  = ", fb_h);
                        break;
                    }
                }
            }
        }
    }

    if (!fb_addr || fb_w == 0) {
        serial_write("[FATAL] No framebuffer found\n");
        terminal_write_colored("FATAL: No framebuffer found. Check GRUB gfxpayload.\n",
            VGA_ENTRY_COLOR(VGA_RED, VGA_BLACK));
        __asm__ volatile("cli");
        while (1) __asm__ volatile("hlt");
    }

    if ((u32)fb_addr >= 0x100000u)
        paging_map_mmio((u32)fb_addr, fb_h * fb_pitch + 0x400000u);

    slog_u32("  FB ready: bpp=", fb_bpp);

    /* Stage 5: GUI */
    slog_stage(5, "GUI launch");
    gfx_set_pixel_format(fb_r_pos, fb_g_pos, fb_b_pos);
    serial_write("  Calling gui_init...\n");
    gui_init((u32*)fb_addr, fb_w, fb_h, fb_pitch);
    serial_write("  gui_init complete\n");
    vesa_init();          slog_ok("VESA/BGA");

    serial_write("  Calling gui_run (enters main loop)...\n");
    gui_run();

    serial_write("[WARN] gui_run returned, falling back to shell\n");
    terminal_init();
    shell_run();
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}
