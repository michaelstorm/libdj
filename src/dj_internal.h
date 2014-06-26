#ifndef DJ_INTERNAL_H
#define DJ_INTERNAL_H

#include <fcntl.h> // required for time_t, ino_t, etc. used by ext2fs.h
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>

#include "dj.h"

struct inode_list
{
    ext2_ino_t index;
    char *path;
    uint64_t len;
    struct inode_list *next;

    struct block_list *blocks_start;
    struct block_list *blocks_end;
};

struct stripe
{
    char *data;
    e2_blkcnt_t references;

    // total number of consecutive blocks, excluding gaps
    e2_blkcnt_t consecutive_blocks;

    // total length of consecutive blocks in bytes, including gaps
    size_t consecutive_len;
};

struct stripe_pointer
{
    struct stripe *stripe;
    off_t pos;
    size_t len;
};

struct block_list
{
    struct inode_cb_info *inode_info;
    blk64_t physical_block;
    e2_blkcnt_t logical_block;
    struct stripe_pointer stripe_ptr;
    struct block_list *next;
};

struct inode_cb_info
{
    ext2_ino_t inode;
    char *path;
    uint64_t len;
    e2_blkcnt_t blocks_read;
    e2_blkcnt_t blocks_scanned;
    struct heap *block_cache;
    void *cb_private;
    int references;
};

struct scan_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct inode_list *inode_list;
};

#endif
