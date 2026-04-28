#pragma once
#include "kernel.h"

#define EXT2_MAGIC        0xEF53u
#define EXT2_ROOT_INODE   2u
#define EXT2_S_IFREG      0x8000u
#define EXT2_S_IFDIR      0x4000u
#define EXT2_FT_REG_FILE  1
#define EXT2_FT_DIR       2

typedef struct {
    u32 s_inodes_count, s_blocks_count, s_r_blocks_count;
    u32 s_free_blocks_count, s_free_inodes_count, s_first_data_block;
    u32 s_log_block_size, s_log_frag_size;
    u32 s_blocks_per_group, s_frags_per_group, s_inodes_per_group;
    u32 s_mtime, s_wtime;
    u16 s_mnt_count, s_max_mnt_count, s_magic, s_state, s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck, s_checkinterval, s_creator_os, s_rev_level;
    u16 s_def_resuid, s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size, s_block_group_nr;
    u32 s_feature_compat, s_feature_incompat, s_feature_ro_compat;
    u8  s_uuid[16], s_volume_name[16], s_last_mounted[64];
    u32 s_algo_bitmap;
    u8  _pad[820];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    u32 bg_block_bitmap, bg_inode_bitmap, bg_inode_table;
    u16 bg_free_blocks_count, bg_free_inodes_count, bg_used_dirs_count, bg_pad;
    u8  bg_reserved[12];
} __attribute__((packed)) ext2_bgd_t;

typedef struct {
    u16 i_mode; u16 i_uid; u32 i_size;
    u32 i_atime, i_ctime, i_mtime, i_dtime;
    u16 i_gid, i_links_count; u32 i_blocks, i_flags, i_osd1;
    u32 i_block[15]; /* [0-11] direct, [12] single-indirect, [13] double, [14] triple */
    u32 i_generation, i_file_acl, i_dir_acl, i_faddr;
    u8  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    u32 de_inode; u16 de_rec_len; u8 de_name_len, de_file_type;
    char de_name[];
} __attribute__((packed)) ext2_dirent_t;

typedef struct {
    u32 inode;
    u8  file_type;
    char name[256];
} ext2_dirent_info_t;

/* Public API */
int  ext2_mount(void);
int  ext2_read_inode(u32 ino, ext2_inode_t *out);
int  ext2_read_data(const ext2_inode_t *ino, u32 off, void *buf, u32 len);
u32  ext2_lookup(u32 dir_ino, const char *name);
int  ext2_list_dir(u32 dir_ino, ext2_dirent_info_t *out, u32 max_entries, u32 *out_count);
u32  ext2_path_to_inode(const char *path);
int  ext2_write_data(u32 ino_num, u32 off, const void *buf, u32 len);
u32  ext2_create_file(u32 parent_ino, const char *name);
u32  ext2_mkdir(u32 parent_ino, const char *name);
int  ext2_unlink(u32 parent_ino, const char *name);
