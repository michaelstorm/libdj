#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "clog.h"
#include "dir_scan.h"
#include "dj_internal.h"
#include "util.h"

char *dir_path_append_name(struct dir_tree_entry *dir, char *name)
{
    char *path;
    if (dir == NULL)
    {
        path = malloc(strlen(name)+1);
        strcpy(path, name);
    }
    else
    {
        size_t dir_path_len = strlen(dir->path);
        path = malloc(dir_path_len + strlen(name) + 2);
        char *sep = dir_path_len > 0 && dir->path[dir_path_len-1] == '/' ? "" : "/";
        sprintf(path, "%s%s%s", dir->path, sep, name);
    }
    return path;
}

void dir_entry_add_file(ext2_ino_t ino, char *name,
                        struct dir_entry_cb_data *cb_data, uint64_t len)
{
    struct inode_list *list = ecalloc(sizeof(struct inode_list));
    list->index = ino;
    list->len = len;
    list->path = dir_path_append_name(cb_data->dir, name);

    if (cb_data->list_start == NULL)
    {
        cb_data->list_start = list;
        cb_data->list_end = list;
    }
    else
    {
        cb_data->list_end->next = list;
        cb_data->list_end = list;
    }
}

int dir_entry_cb(ext2_ino_t dir_ino, int entry, struct ext2_dir_entry *dirent,
                 int offset, int blocksize, char *buf, void *private)
{
    if (entry == DIRENT_OTHER_FILE)
    {
        // copy the entry's name into a new buffer
        int name_len = dirent->name_len & 0xFF; // saw the 0xFF in e2fsprogs
                                                // sources; don't know why the high
                                                // bits would be set
        char name[name_len+1];
        memcpy(name, dirent->name, name_len);
        name[name_len] = '\0';

        struct dir_entry_cb_data *cb_data = private;

        // read the entry's inode contents
        struct ext2_inode inode_contents;
        CHECK_FATAL(ext2fs_read_inode(cb_data->fs, dirent->inode,
                                      &inode_contents),
                "while reading inode contents");

        if (LINUX_S_ISDIR(inode_contents.i_mode))
        {
            // if it's a directory, create a new leaf object in the in-memory
            // tree containing the fully-qualified name (for convenience) and a
            // reference to the parent
            struct dir_tree_entry dir;
            dir.parent = cb_data->dir;
            dir.path = dir_path_append_name(dir.parent, name);

            // TODO I think this size is mandated by the docs?
            char block_buf[cb_data->fs->blocksize*3];

            // aaaaand recurse
            cb_data->dir = &dir;

            LogDebug("Entering directory %s", dir.path);
            CHECK_FATAL(ext2fs_dir_iterate2(cb_data->fs, dirent->inode, 0,
                                            block_buf, dir_entry_cb, cb_data),
                    "while iterating over directory %s", name);
            LogDebug("Left directory %s", dir.path);

            cb_data->dir = cb_data->dir->parent;

            // whee memory leaks
            free(dir.path);
        }
        else if (!S_ISLNK(inode_contents.i_mode))
        {
            LogDebug("Adding file %s", name);
            // if it's a file, add it to the linked list that was passed it (and
            // therefore shared by all directories that we're interested in)
            dir_entry_add_file(dirent->inode, name, cb_data,
                               inode_contents.i_size);
        }
    }

    return 0;
}
