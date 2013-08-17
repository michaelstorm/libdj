#ifndef EXT_BATCHING_H
#define EXT_BATCHING_H

#include <stdint.h>

typedef int (*block_cb)(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private);

void initialize_ext_batching(char *error_prog_name);
void iterate_dir(char *dev_path, char *dir_path, block_cb cb);

#endif
