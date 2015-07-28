#ifndef DJ_BLOCK_SCAN_H
#define DJ_BLOCK_SCAN_H

#include "dj_internal.h"

void scan_blocks(ext2_filsys fs, block_cb cb, struct inode_list *inode_list);

#endif