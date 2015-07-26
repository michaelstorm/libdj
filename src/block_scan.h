#ifndef DJ_BLOCK_SCAN_H
#define DJ_BLOCK_SCAN_H

#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>
#include <stdint.h>

#include "dj_internal.h"

struct scan_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct inode_list *inode_list;
};

void scan_block(uint64_t block_size, blk64_t physical_block,
                e2_blkcnt_t logical_block, struct scan_blocks_info *scan_info);
int scan_block_cb(ext2_filsys fs, blk64_t *blocknr, e2_blkcnt_t blockcnt,
                  blk64_t ref_blk, int ref_offset, void *private);
void scan_blocks(ext2_filsys fs, block_cb cb, struct inode_list *inode_list);

#endif