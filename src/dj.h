#ifndef DJ_H
#define DJ_H

#include <stdint.h>

#define ITERATE_OPT_DIRECT 1

typedef int (*block_cb)(uint32_t inode, char *path, uint64_t pos,
			            uint64_t file_len, char *data, uint64_t data_len,
			            void **private);

void dj_init(char *error_prog_name);
void dj_free();
void dj_read(char *dev_path, char *dir_path, block_cb cb, int max_inodes,
			 int max_blocks, int coalesce_distance, int flags, int advice_flags);

#endif
