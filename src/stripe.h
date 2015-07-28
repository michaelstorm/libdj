#ifndef DJ_STRIPE_H
#define DJ_STRIPE_H

#include "dj_internal.h"

struct stripe *next_stripe(uint64_t block_size, int coalesce_distance,
                           int max_inode_blocks, struct block_list *block_list);

void read_stripe_data(off_t block_size, blk64_t physical_block, int direct,
                      int fd, struct stripe *stripe);

struct block_list *heapify_stripe(ext2_filsys fs, block_cb cb,
                                  struct block_list *block_list,
                                  struct stripe *stripe, int max_inode_blocks,
                                  int *open_inodes_count);

#endif