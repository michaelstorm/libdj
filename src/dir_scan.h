#ifndef DJ_DIR_SCAN_H
#define DJ_DIR_SCAN_H

#include <ext2fs/ext2fs.h>

struct inode_list *get_inode_list(ext2_filsys fs, char *target_path);

#endif
