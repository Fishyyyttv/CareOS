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

/* Forward decls: the block map below is shared by the read and write paths,
 * so it needs the allocator that is defined further down with the rest of
 * the write helpers. */
static u32 alloc_block(void);

/* Allocate a block and zero it on disk, so a freshly linked data block reads
 * back as zeros and a freshly linked indirect block contains only null
 * pointers. Returns 0 on failure. */
static u32 alloc_zeroed_block(void) {
    u8 *zero = (u8*)kmalloc(block_size);
    if (!zero) return 0;
    u32 b = alloc_block();
    if (b == 0) { kfree(zero); return 0; }
    kmemset(zero, 0, block_size);
    int r = write_block(b, zero);
    kfree(zero);
    return r == 0 ? b : 0;
}

/* Read entry `index` out of indirect block `ind_block`. When `alloc` is set
 * and the slot is empty, allocate a block, link it in and write the indirect
 * block back. *allocated reports whether a new block was consumed so the
 * caller can keep i_blocks accurate. */
static int ind_entry(u32 ind_block, u32 index, bool alloc,
                     u32 *out, bool *allocated) {
    u32 *tbl = (u32*)kmalloc(block_size);
    if (!tbl) return -1;
    if (read_block(ind_block, tbl) != 0) { kfree(tbl); return -1; }

    u32 v = tbl[index];
    if (v == 0 && alloc) {
        v = alloc_zeroed_block();
        if (v == 0) { kfree(tbl); return -1; }
        tbl[index] = v;
        if (write_block(ind_block, tbl) != 0) { kfree(tbl); return -1; }
        if (allocated) *allocated = true;
    }
    kfree(tbl);
    *out = v;
    return 0;
}

/*
 * Map a file-logical block number onto its physical block, walking the
 * direct, single-, double- and triple-indirect chains.
 *
 * alloc == false  read path: a missing block yields *out_phys == 0, i.e. a
 *                 sparse hole. Never mutates *ino.
 * alloc == true   write path: missing data blocks *and* the indirect blocks
 *                 needed to reach them are allocated and linked in. i_blocks
 *                 is updated for every block consumed (indirect blocks
 *                 included, as ext2 requires) and *dirty is set so the caller
 *                 knows the inode must be written back.
 *
 * Returns 0 on success, negative on I/O failure, allocation failure, or a
 * logical block beyond the triple-indirect limit.
 */
static int map_block(ext2_inode_t *ino, u32 logical, bool alloc,
                     u32 *out_phys, bool *dirty) {
    const u32 ptrs = block_size / 4;
    *out_phys = 0;

    /* [0,12) direct */
    if (logical < 12) {
        u32 b = ino->i_block[logical];
        if (b == 0 && alloc) {
            b = alloc_zeroed_block();
            if (b == 0) return -1;
            ino->i_block[logical] = b;
            ino->i_blocks += sectors_per_block;
            if (dirty) *dirty = true;
        }
        *out_phys = b;
        return 0;
    }
    logical -= 12;

    /* Pick the indirection level and rebase `logical` within it. */
    u32 slot, depth;
    if (logical < ptrs) {
        slot = 12; depth = 1;
    } else if (logical - ptrs < ptrs * ptrs) {
        slot = 13; depth = 2; logical -= ptrs;
    } else {
        logical -= ptrs + ptrs * ptrs;
        if (logical >= ptrs * ptrs * ptrs) return -1;  /* past triple-indirect */
        slot = 14; depth = 3;
    }

    /* Root of the chain lives in i_block[slot]. */
    u32 cur = ino->i_block[slot];
    if (cur == 0) {
        if (!alloc) return 0;                  /* sparse: no chain at all */
        cur = alloc_zeroed_block();
        if (cur == 0) return -1;
        ino->i_block[slot] = cur;
        ino->i_blocks += sectors_per_block;
        if (dirty) *dirty = true;
    }

    /* Descend one indirect block per level. At level L each entry covers
     * ptrs^(L-1) logical blocks, so the index at that level is
     * (logical / span) % ptrs; at the last level span == 1 and `cur` ends up
     * holding the data block itself. */
    for (u32 level = depth; level > 0; level--) {
        u32 span = 1;
        for (u32 k = 1; k < level; k++) span *= ptrs;
        u32 index = (logical / span) % ptrs;

        u32 next = 0;
        bool allocated = false;
        if (ind_entry(cur, index, alloc, &next, &allocated) != 0) return -1;
        if (allocated) {
            ino->i_blocks += sectors_per_block;
            if (dirty) *dirty = true;
        }
        if (next == 0) return 0;               /* sparse hole (alloc == false) */
        cur = next;
    }

    *out_phys = cur;
    return 0;
}

/* Read-only block lookup. map_block with alloc == false never writes through
 * the pointer, so dropping const here is safe. */
static u32 resolve_block(const ext2_inode_t *ino, u32 logical) {
    u32 phys = 0;
    if (map_block((ext2_inode_t*)ino, logical, false, &phys, NULL) != 0) return 0;
    return phys;
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
                    /* The last group's bitmap is padded out to a whole block;
                     * those trailing bits address blocks that do not exist.
                     * Refuse to hand one out rather than scribbling past the
                     * end of the filesystem. */
                    u32 blk = sb.s_first_data_block + g * blocks_per_group
                            + byte * 8 + bit;
                    if (blk >= sb.s_blocks_count) { kfree(bitmap); return 0; }

                    bitmap[byte] |= (1u << bit);
                    write_block(bgd.bg_block_bitmap, bitmap);
                    bgd.bg_free_blocks_count--;
                    sb.s_free_blocks_count--;
                    write_bgd(g, &bgd);
                    write_sectors(2, 2, &sb);
                    kfree(bitmap);
                    return blk;
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

    /* Need a new block for the directory. map_block allocates the block and
     * any indirect blocks needed to reach it, and keeps i_blocks in step. */
    u32 new_block = 0;
    bool dirty = false;
    if (map_block(&parent, logical, true, &new_block, &dirty) != 0 || new_block == 0) {
        kfree(blk); return -3;
    }

    kmemset(blk, 0, block_size);
    ext2_dirent_t *de = (ext2_dirent_t*)blk;
    de->de_inode     = child_ino;
    de->de_rec_len   = (u16)block_size;
    de->de_name_len  = (u8)name_len;
    de->de_file_type = file_type;
    kmemcpy(de->de_name, name, name_len);
    if (write_block(new_block, blk) != 0) { kfree(blk); return -4; }

    parent.i_size += block_size;
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
    bool dirty = false;
    while (written < len) {
        u32 logical   = (off + written) / block_size;
        u32 blk_off   = (off + written) % block_size;
        u32 chunk     = block_size - blk_off;
        if (chunk > len - written) chunk = len - written;

        /* map_block allocates the data block plus any single-, double- or
         * triple-indirect blocks needed to address it, so writes are no
         * longer capped at the 12 direct blocks. Newly allocated blocks are
         * already zeroed on disk, so the read-modify-write below is correct
         * for a partial first/last block. */
        u32 phys = 0;
        if (map_block(&inode, logical, true, &phys, &dirty) != 0 || phys == 0) {
            kfree(blk); return -4;
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
