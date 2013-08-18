#include <stdio.h>
#include <stdlib.h>
#include <openssl/md5.h>

#include "ext-batching.h"

int file_md5(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private);
