#ifndef DJ_MD5_H
#define DJ_MD5_H

int file_md5(uint32_t inode, char *path, uint64_t pos, uint64_t file_len,
			 char *data, uint64_t data_len, void **private);

#endif
