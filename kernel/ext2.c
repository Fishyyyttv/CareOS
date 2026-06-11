/* =============================================================================
 * CareOS - kernel/ext2.c
 * ext2 filesystem driver: mount, inode read, data read, directory lookup,
 * and path resolution.
 * ============================================================================= */
#include "kernel.h"
#include "ext2.h"

static ext2_superblock_t sb;
static u32 block_size;
static u32 sectors_per_block;
static u32 inodes_per_group;
static u32 blocks_per_group;
static u32 num_groups;
static bool ext2_ready = false;

static int write_block(u32 block_num, const void *buf);

static int read_sectors(u32 lba, u32 count, void *buf) {
    /* ata_read_sectors takes u8 count — split if > 255 */
    u32 done = 0;
    while (done < count) {
        u32 chunk = count - done;
        if (chunk > 255) chunk = 255;
        if (ata_read_sectors(lba + done, (u8)chunk, (u8*)buf + done * 512) != 0)
            return -1;
        done += chunk;
    }
    return 0;
}

static int read_block(u32 bn, void *buf) {
    return read_sectors(bn * sectors_per_block, sectors_per_block, buf);
}

int ext2_mount(void) {
    /* Superblock at byte offset 1024 = LBA 2 */
    if (read_sectors(2, 2, &sb) != 0) {
        serial_write("[ext2] superblock read failed\n"); return -1;
    }
    if (sb.s_magic != EXT2_MAGIC) {
        serial_write("[ext2] bad magic -- run make format-disk\n"); return -2;
    }
    block_size        = 1024u << sb.s_log_block_size;
    sectors_per_block = block_size / 512;
    inodes_per_group  = sb.s_inodes_per_group;
    blocks_per_group  = sb.s_blocks_per_group;
    num_groups        = (sb.s_blocks_count + blocks_per_group - 1) / blocks_per_group;
    ext2_ready        = true;
    serial_write("[ext2] mounted, block_size=");
    char buf[12]; kutoa(block_size, buf, 10); serial_write(buf);
    serial_write(" groups="); kutoa(num_groups, buf, 10); serial_write(buf);
    serial_write("\n");
    return 0;
}

static int read_bgd(u32 group, ext2_bgd_t *out) {
    u32 bgds_per_block = block_size / sizeof(ext2_bgd_t);
    u32 bgdt_block = sb.s_first_data_block + 1 + (group / bgds_per_block);
    u32 bgdt_off = (group % bgds_per_block) * sizeof(ext2_bgd_t);
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -1;
    int r = read_block(bgdt_block, blk);
    if (r == 0) kmemcpy(out, blk + bgdt_off, sizeof(ext2_bgd_t));
    kfree(blk);
    return r;
}

static int write_bgd(u32 group, const ext2_bgd_t *src) {
    u32 bgds_per_block = block_size / sizeof(ext2_bgd_t);
    u32 bgdt_block = sb.s_first_data_block + 1 + (group / bgds_per_block);
    u32 bgdt_off = (group % bgds_per_block) * sizeof(ext2_bgd_t);
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -1;
    if (read_block(bgdt_block, blk) != 0) {
        kfree(blk);
        return -2;
    }
    kmemcpy(blk + bgdt_off, src, sizeof(*src));
    int r = write_block(bgdt_block, blk);
    kfree(blk);
    return r;
}

int ext2_read_inode(u32 ino, ext2_inode_t *out) {
    if (!ext2_ready || ino == 0) return -1;
    u32 group = (ino - 1) / inodes_per_group;
    u32 index = (ino - 1) % inodes_per_group;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -2;
    u32 inode_size       = sb.s_inode_size ? sb.s_inode_size : 128;
    u32 inodes_per_block = block_size / inode_size;
    u32 block_num        = bgd.bg_inode_table + index / inodes_per_block;
    u32 block_off        = (index % inodes_per_block) * inode_size;
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -3;
    int r = read_block(block_num, blk);
    if (r == 0) kmemcpy(out, blk + block_off, sizeof(ext2_inode_t));
    kfree(blk);
    return r;
}

static u32 resolve_block(const ext2_inode_t *ino, u32 logical) {
    u32 ptrs = block_size / 4;
    if (logical < 12) return ino->i_block[logical];
    logical -= 12;
    if (logical < ptrs) {
        if (ino->i_block[12] == 0) return 0;
        u32 *ind = (u32*)kmalloc(block_size);
        if (!ind) return 0;
        if (read_block(ino->i_block[12], ind) != 0) { kfree(ind); return 0; }
        u32 r = ind[logical]; kfree(ind); return r;
    }
    logical -= ptrs;
    if (logical < ptrs * ptrs) {
        if (ino->i_block[13] == 0) return 0;
        u32 *dind = (u32*)kmalloc(block_size);
        if (!dind) return 0;
        u32 *ind  = (u32*)kmalloc(block_size);
        if (!ind) { kfree(dind); return 0; }
        if (read_block(ino->i_block[13], dind) != 0) { kfree(dind); kfree(ind); return 0; }
        if (read_block(dind[logical / ptrs], ind) != 0) { kfree(dind); kfree(ind); return 0; }
        u32 r = ind[logical % ptrs]; kfree(dind); kfree(ind); return r;
    }
    return 0;
}

int ext2_read_data(const ext2_inode_t *ino, u32 off, void *buf, u32 len) {
    if (!ext2_ready || !buf) return -1;
    if (off >= ino->i_size) return 0;
    if (off + len > ino->i_size) len = ino->i_size - off;
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;
    u32 written = 0;
    while (written < len) {
        u32 logical   = (off + written) / block_size;
        u32 blk_off   = (off + written) % block_size;
        u32 chunk     = block_size - blk_off;
        if (chunk > len - written) chunk = len - written;
        u32 phys = resolve_block(ino, logical);
        if (phys == 0 || read_block(phys, blk) != 0) { kfree(blk); return -3; }
        kmemcpy((u8*)buf + written, blk + blk_off, chunk);
        written += chunk;
    }
    kfree(blk);
    return (int)written;
}

/* ── Task 8: directory lookup and path resolution ─────────────────────────── */

u32 ext2_lookup(u32 dir_ino, const char *name) {
    ext2_inode_t inode;
    if (ext2_read_inode(dir_ino, &inode) != 0) return 0;
    if ((inode.i_mode & 0xF000u) != EXT2_S_IFDIR) return 0;
    u32 name_len = (u32)kstrlen(name);
    u8 *buf = (u8*)kmalloc(block_size);
    if (!buf) return 0;
    u32 off = 0;
    while (off < inode.i_size) {
        u32 phys = resolve_block(&inode, off / block_size);
        if (phys == 0 || read_block(phys, buf) != 0) break;
        u32 blk_off = 0;
        while (blk_off < block_size) {
            ext2_dirent_t *de = (ext2_dirent_t*)(buf + blk_off);
            if (de->de_rec_len == 0) break;
            if (de->de_inode != 0 && de->de_name_len == name_len &&
                kmemcmp(de->de_name, name, name_len) == 0) {
                u32 result = de->de_inode;
                kfree(buf); return result;
            }
            blk_off += de->de_rec_len;
        }
        off += block_size;
    }
    kfree(buf); return 0;
}

int ext2_list_dir(u32 dir_ino, ext2_dirent_info_t *out, u32 max_entries, u32 *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_entries == 0) return 0;

    ext2_inode_t inode;
    if (ext2_read_inode(dir_ino, &inode) != 0) return -1;
    if ((inode.i_mode & 0xF000u) != EXT2_S_IFDIR) return -2;

    u8 *buf = (u8*)kmalloc(block_size);
    if (!buf) return -3;

    u32 count = 0;
    u32 off = 0;
    while (off < inode.i_size && count < max_entries) {
        u32 phys = resolve_block(&inode, off / block_size);
        if (phys == 0 || read_block(phys, buf) != 0) {
            kfree(buf);
            return -4;
        }

        u32 blk_off = 0;
        while (blk_off + sizeof(ext2_dirent_t) <= block_size && count < max_entries) {
            ext2_dirent_t *de = (ext2_dirent_t*)(buf + blk_off);
            if (de->de_rec_len < 8 || blk_off + de->de_rec_len > block_size) break;

            if (de->de_inode != 0 && de->de_name_len != 0) {
                u32 copy_len = de->de_name_len;
                if (copy_len > 255) copy_len = 255;

                if (!(copy_len == 1 && de->de_name[0] == '.') &&
                    !(copy_len == 2 && de->de_name[0] == '.' && de->de_name[1] == '.')) {
                    out[count].inode = de->de_inode;
                    out[count].file_type = de->de_file_type;
                    kmemcpy(out[count].name, de->de_name, copy_len);
                    out[count].name[copy_len] = '\0';
                    count++;
                }
            }

            blk_off += de->de_rec_len;
        }

        off += block_size;
    }

    if (out_count) *out_count = count;
    kfree(buf);
    return 0;
}

u32 ext2_path_to_inode(const char *path) {
    if (!path || path[0] != '/') return 0;
    u32 ino = EXT2_ROOT_INODE;
    char component[256];
    const char *p = path + 1;
    while (*p) {
        u32 i = 0;
        while (*p && *p != '/' && i < 255) component[i++] = *p++;
        component[i] = '\0';
        if (*p == '/') p++;
        if (i == 0) continue;
        ino = ext2_lookup(ino, component);
        if (ino == 0) return 0;
    }
    return ino;
}

/* ── Write-path helpers (Task 9) ──────────────────────────────────────────── */

static int write_sectors(u32 lba, u32 count, const void *buf) {
    u32 done = 0;
    while (done < count) {
        u32 chunk = count - done;
        if (chunk > 255) chunk = 255;
        if (ata_write_sectors(lba + done, (u8)chunk, (const u8*)buf + done * 512) != 0)
            return -1;
        done += chunk;
    }
    return 0;
}

static int write_block(u32 block_num, const void *buf) {
    return write_sectors(block_num * sectors_per_block, sectors_per_block, buf);
}

/* Allocate a free block. Returns block number or 0 on failure. */
static u32 alloc_block(void) {
    u8 *bitmap = (u8*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (u32 g = 0; g < num_groups; g++) {
        ext2_bgd_t bgd;
        if (read_bgd(g, &bgd) != 0) continue;
        if (bgd.bg_free_blocks_count == 0) continue;

        if (read_block(bgd.bg_block_bitmap, bitmap) != 0) continue;
        u32 scan_bytes = (blocks_per_group + 7) / 8;
        for (u32 byte = 0; byte < scan_bytes; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            for (u32 bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    write_block(bgd.bg_block_bitmap, bitmap);
                    bgd.bg_free_blocks_count--;
                    sb.s_free_blocks_count--;
                    write_bgd(g, &bgd);
                    write_sectors(2, 2, &sb);
                    kfree(bitmap);
                    return sb.s_first_data_block + g * blocks_per_group + byte * 8 + bit;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;  /* disk full */
}

/* Allocate a free inode. Returns inode number or 0 on failure. */
static u32 alloc_inode(void) {
    u8 *bitmap = (u8*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (u32 g = 0; g < num_groups; g++) {
        ext2_bgd_t bgd;
        if (read_bgd(g, &bgd) != 0) continue;
        if (bgd.bg_free_inodes_count == 0) continue;

        if (read_block(bgd.bg_inode_bitmap, bitmap) != 0) continue;
        u32 scan_bytes = (inodes_per_group + 7) / 8;
        for (u32 byte = 0; byte < scan_bytes; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            for (u32 bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    write_block(bgd.bg_inode_bitmap, bitmap);
                    bgd.bg_free_inodes_count--;
                    sb.s_free_inodes_count--;
                    write_bgd(g, &bgd);
                    write_sectors(2, 2, &sb);
                    kfree(bitmap);
                    /* Inode numbers are 1-based; index in group is 0-based */
                    return g * inodes_per_group + byte * 8 + bit + 1;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;
}

static int write_inode(u32 ino, const ext2_inode_t *src) {
    u32 group  = (ino - 1) / inodes_per_group;
    u32 index  = (ino - 1) % inodes_per_group;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;

    u32 inode_size       = sb.s_inode_size ? sb.s_inode_size : 128;
    u32 inodes_per_block = block_size / inode_size;
    u32 block_num        = bgd.bg_inode_table + index / inodes_per_block;
    u32 block_off        = (index % inodes_per_block) * inode_size;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;
    if (read_block(block_num, blk) != 0) { kfree(blk); return -3; }
    kmemcpy(blk + block_off, src, sizeof(ext2_inode_t));
    int r = write_block(block_num, blk);
    kfree(blk);
    return r;
}

/* Add a directory entry to parent directory */
static int add_dirent(u32 parent_ino, u32 child_ino,
                      const char *name, u8 file_type) {
    ext2_inode_t parent;
    if (ext2_read_inode(parent_ino, &parent) != 0) return -1;

    u32 name_len  = (u32)kstrlen(name);
    u32 need_len  = (sizeof(ext2_dirent_t) + name_len + 3) & ~3u;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;

    /* Walk blocks of the parent directory looking for free space */
    u32 logical = 0;
    while (logical * block_size < parent.i_size) {
        u32 phys = resolve_block(&parent, logical);
        if (phys == 0) break;
        if (read_block(phys, blk) != 0) { logical++; continue; }

        u32 pos = 0;
        while (pos < block_size) {
            ext2_dirent_t *de = (ext2_dirent_t*)(blk + pos);
            if (de->de_rec_len == 0) break;
            u32 real_len = (sizeof(ext2_dirent_t) + de->de_name_len + 3) & ~3u;
            u32 slack    = de->de_rec_len - real_len;
            if (de->de_inode == 0 && de->de_rec_len >= need_len) {
                /* Empty slot — reuse it */
                de->de_inode     = child_ino;
                de->de_name_len  = (u8)name_len;
                de->de_file_type = file_type;
                kmemcpy(de->de_name, name, name_len);
                write_block(phys, blk);
                kfree(blk); return 0;
            } else if (de->de_inode != 0 && slack >= need_len) {
                /* Split the record */
                ext2_dirent_t *new_de = (ext2_dirent_t*)(blk + pos + real_len);
                new_de->de_inode     = child_ino;
                new_de->de_rec_len   = (u16)slack;
                new_de->de_name_len  = (u8)name_len;
                new_de->de_file_type = file_type;
                kmemcpy(new_de->de_name, name, name_len);
                de->de_rec_len = (u16)real_len;
                write_block(phys, blk);
                kfree(blk); return 0;
            }
            pos += de->de_rec_len;
        }
        logical++;
    }

    /* Need a new block for the directory */
    if (logical >= 12) { kfree(blk); return -3; } /* no indirect in write path */
    u32 new_block = alloc_block();
    if (new_block == 0) { kfree(blk); return -3; }
    kmemset(blk, 0, block_size);
    ext2_dirent_t *de = (ext2_dirent_t*)blk;
    de->de_inode     = child_ino;
    de->de_rec_len   = (u16)block_size;
    de->de_name_len  = (u8)name_len;
    de->de_file_type = file_type;
    kmemcpy(de->de_name, name, name_len);
    write_block(new_block, blk);

    parent.i_block[logical] = new_block;
    parent.i_size += block_size;
    parent.i_blocks += sectors_per_block;
    write_inode(parent_ino, &parent);
    kfree(blk);
    return 0;
}

/* ── Write-path implementations (Task 9) ───────────────────────────────────── */

int ext2_write_data(u32 ino_num, u32 off, const void *buf, u32 len) {
    if (!ext2_ready) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino_num, &inode) != 0) return -2;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -3;

    u32 written = 0;
    while (written < len) {
        u32 logical   = (off + written) / block_size;
        u32 blk_off   = (off + written) % block_size;
        u32 chunk     = block_size - blk_off;
        if (chunk > len - written) chunk = len - written;

        u32 phys = resolve_block(&inode, logical);
        if (phys == 0) {
            /* Allocate a new block — direct blocks only */
            if (logical >= 12) { kfree(blk); return -5; }
            phys = alloc_block();
            if (phys == 0) { kfree(blk); return -4; }
            kmemset(blk, 0, block_size);
            write_block(phys, blk);
            inode.i_block[logical] = phys;
            inode.i_blocks += sectors_per_block;
        }

        if (read_block(phys, blk) != 0) { kfree(blk); return -6; }
        kmemcpy(blk + blk_off, (const u8*)buf + written, chunk);
        if (write_block(phys, blk) != 0) { kfree(blk); return -7; }
        written += chunk;
    }

    if (off + len > inode.i_size) inode.i_size = off + len;
    write_inode(ino_num, &inode);
    kfree(blk);
    return (int)written;
}

u32 ext2_create_file(u32 parent_ino, const char *name) {
    u32 ino = alloc_inode();
    if (ino == 0) return 0;

    ext2_inode_t inode;
    kmemset(&inode, 0, sizeof(inode));
    inode.i_mode        = EXT2_S_IFREG | 0644;
    inode.i_links_count = 1;
    if (write_inode(ino, &inode) != 0) return 0;
    if (add_dirent(parent_ino, ino, name, EXT2_FT_REG_FILE) != 0) return 0;
    return ino;
}

u32 ext2_mkdir(u32 parent_ino, const char *name) {
    u32 ino = alloc_inode();
    if (ino == 0) return 0;

    u32 new_block = alloc_block();
    if (new_block == 0) return 0;

    /* Write . and .. entries */
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return 0;
    kmemset(blk, 0, block_size);

    ext2_dirent_t *dot = (ext2_dirent_t*)blk;
    dot->de_inode = ino; dot->de_rec_len = 12;
    dot->de_name_len = 1; dot->de_file_type = EXT2_FT_DIR;
    dot->de_name[0] = '.';

    ext2_dirent_t *dotdot = (ext2_dirent_t*)(blk + 12);
    dotdot->de_inode     = parent_ino;
    dotdot->de_rec_len   = (u16)(block_size - 12);
    dotdot->de_name_len  = 2; dotdot->de_file_type = EXT2_FT_DIR;
    dotdot->de_name[0]   = '.'; dotdot->de_name[1] = '.';
    write_block(new_block, blk);
    kfree(blk);

    ext2_inode_t inode;
    kmemset(&inode, 0, sizeof(inode));
    inode.i_mode        = EXT2_S_IFDIR | 0755;
    inode.i_links_count = 2;
    inode.i_size        = block_size;
    inode.i_blocks      = sectors_per_block;
    inode.i_block[0]    = new_block;
    if (write_inode(ino, &inode) != 0) {
        serial_write("[ext2] mkdir: write_inode failed\n"); return 0;
    }
    if (add_dirent(parent_ino, ino, name, EXT2_FT_DIR) != 0) {
        serial_write("[ext2] mkdir: add_dirent failed\n"); return 0;
    }

    ext2_inode_t parent;
    if (ext2_read_inode(parent_ino, &parent) == 0) {
        parent.i_links_count++;
        if (write_inode(parent_ino, &parent) != 0)
            serial_write("[ext2] mkdir: parent link update failed\n");
    }

    u32 group = (ino - 1) / inodes_per_group;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) == 0) {
        bgd.bg_used_dirs_count++;
        if (write_bgd(group, &bgd) != 0)
            serial_write("[ext2] mkdir: bgd dir count update failed\n");
    }

    return ino;
}

int ext2_unlink(u32 parent_ino, const char *name) {
    ext2_inode_t parent;
    if (ext2_read_inode(parent_ino, &parent) != 0) return -1;
    if ((parent.i_mode & 0xF000u) != EXT2_S_IFDIR) return -2;

    u32 name_len = (u32)kstrlen(name);
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -3;

    u32 logical = 0;
    while (logical * block_size < parent.i_size) {
        u32 phys = resolve_block(&parent, logical);
        if (phys == 0) break;
        if (read_block(phys, blk) != 0) {
            logical++;
            continue;
        }

        u32 pos = 0;
        while (pos + sizeof(ext2_dirent_t) <= block_size) {
            ext2_dirent_t *de = (ext2_dirent_t*)(blk + pos);
            if (de->de_rec_len < 8 || pos + de->de_rec_len > block_size) break;

            if (de->de_inode != 0 && de->de_name_len == name_len &&
                kmemcmp(de->de_name, name, name_len) == 0) {
                de->de_inode = 0;
                if (write_block(phys, blk) != 0) {
                    kfree(blk);
                    return -4;
                }
                kfree(blk);
                return 0;
            }

            pos += de->de_rec_len;
        }

        logical++;
    }

    kfree(blk);
    return -5;
}
