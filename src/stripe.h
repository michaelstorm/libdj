#include <stdint.h>

#include "dj_internal.h"

void flush_inode_blocks(uint64_t block_size, struct inode_cb_info *inode_info,
                        block_cb cb, int *open_inodes_count);

struct stripe *next_stripe(uint64_t block_size, int coalesce_distance,
                           int max_inode_blocks, struct block_list *block_list);

void read_stripe_data(off_t block_size, blk64_t physical_block, int direct,
                      int fd, struct stripe *stripe);

struct block_list *heapify_stripe(ext2_filsys fs, block_cb cb,
                                  struct block_list *block_list,
                                  struct stripe *stripe, int max_inode_blocks,
                                  int *open_inodes_count);