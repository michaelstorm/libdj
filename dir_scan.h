#ifndef DJ_DIR_SCAN_H
#define DJ_DIR_SCAN_H

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <stdint.h>

struct dir_tree_entry
{
    char *path;
    struct dir_tree_entry *parent;
};

struct dir_entry_cb_data
{
    ext2_filsys fs;
    struct dir_tree_entry *dir;
    struct inode_list *list_start;
    struct inode_list *list_end;
};

char *dir_path_append_name(struct dir_tree_entry *dir, char *name);
void  dir_entry_add_file(ext2_ino_t ino, char *name,
                         struct dir_entry_cb_data *cb_data, uint64_t len);
int   dir_entry_cb(ext2_ino_t dir_ino, int entry, struct ext2_dir_entry *dirent,
                   int offset, int blocksize, char *buf, void *private);

#endif