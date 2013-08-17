#define _FILE_OFFSET_BITS 64

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <alloca.h>
#include <stdio.h>
#include <stdint.h>
#include <et/com_err.h>
#include <unistd.h>

#include "crc.h"
#include "ext-batching.h"
#include "heap.h"
#include "listsort.h"

void exit_str(char *message, ...)
{
    va_list args;
    va_start(args,  message);
    fprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void *emalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
        exit_str("Error allocating %d bytes of memory", size);
    return ptr;
}

void *ecalloc(size_t size)
{
    void *ptr = calloc(1, size);
    if (ptr == NULL)
        exit_str("Error allocating %d bytes of memory", size);
    return ptr;
}

char *prog_name;

#define CHECK_WARN(FUNC, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
}

#define CHECK_WARN_DO(FUNC, DO, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
    { \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
        DO; \
    } \
}

#define CHECK_FATAL(FUNC, MSG, ...) \
{ \
    errcode_t err = FUNC; \
    if (err != 0) \
    { \
        com_err(prog_name, err, MSG, ##__VA_ARGS__); \
        exit(1); \
    } \
}

struct inode_list
{
    ext2_ino_t index;
    char *name;
    uint64_t len;
    struct dir_tree_entry *dir;
    struct inode_list *next;
};

struct dir_tree_entry
{
    ext2_ino_t inode;
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

struct inode_cb_info
{
    ext2_ino_t inode;
    char *path;
    uint64_t len;
    e2_blkcnt_t blocks_read;
    struct heap *block_cache;
    void *cb_private;
    int references;
};

struct block_list
{
    struct inode_cb_info *inode_info;
    blk64_t physical_block;
    e2_blkcnt_t logical_block;
    char *data;
    struct block_list *next;
};

struct scope_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct block_list *list_start;
    struct block_list *list_end;
};

void dir_entry_add_file(ext2_ino_t ino, char *name, struct dir_entry_cb_data *cb_data)
{
    struct ext2_inode inode_contents;
    CHECK_FATAL(ext2fs_read_inode(cb_data->fs, ino, &inode_contents),
            "while reading inode contents");

    //if (ext2fs_inode_has_valid_blocks(&inode_contents))
    if (!S_ISLNK(inode_contents.i_mode))
    {
        struct inode_list *list = ecalloc(sizeof(struct inode_list));
        list->index = ino;
        list->dir = cb_data->dir;
        list->len = inode_contents.i_size;
        list->name = malloc(strlen(name)+1);
        strcpy(list->name, name);

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
}

int dir_entry_cb(ext2_ino_t dir_ino, int entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *private)
{
    if (entry == DIRENT_OTHER_FILE)
    {
        struct dir_entry_cb_data *cb_data = (struct dir_entry_cb_data *)private;

        // saw the 0xFF in e2fsprogs sources; don't know why the high bits would be set
        int name_len = dirent->name_len & 0xFF;
        char name[name_len+1];
        memcpy(name, dirent->name, name_len);
        name[name_len] = '\0';

        errcode_t dir_check_result = ext2fs_check_directory(cb_data->fs, dirent->inode);
        if (dir_check_result == 0)
        {
            //printf("directory dir: %s, name: %s\n", cb_data->dir->path, name);
            struct dir_tree_entry *dir = ecalloc(sizeof(struct dir_tree_entry));
            dir->inode = dirent->inode;
            dir->parent = cb_data->dir;
            if (cb_data->dir == NULL)
            {
                dir->path = malloc(name_len+1);
                strcpy(dir->path, name);
            }
            else
            {
                dir->path = malloc(strlen(cb_data->dir->path) + name_len + 2);
                sprintf(dir->path, "%s/%s", cb_data->dir->path, name);
            }

            char block_buf[cb_data->fs->blocksize*3];

            cb_data->dir = dir;
            CHECK_FATAL(ext2fs_dir_iterate2(cb_data->fs, dirent->inode, 0, block_buf, dir_entry_cb, cb_data),
                    "while iterating over directory %s", name);
            cb_data->dir = cb_data->dir->parent;
        }
        else if (dir_check_result == EXT2_ET_NO_DIRECTORY)
        {
            //printf("file dir: %s, name: %s\n", cb_data->dir->path, name);
            dir_entry_add_file(dirent->inode, name, cb_data);
        }
        else
            com_err(prog_name, dir_check_result, "while checking if inode %d is a directory", dirent->inode);
    }

    return 0;
}

// inode indexes are unsigned ints, so don't subtract them
SORT_FUNC(inode_list_sort, struct inode_list, (p->index < q->index ? -1 : 1))
SORT_FUNC(block_list_sort, struct block_list, (p->physical_block < q->physical_block ? -1 : 1))

int read_buf_len = 1048576;
char *read_buf;

int print_block_data(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private)
{
    char str_data[data_len+1];
    memcpy(str_data, data, data_len);
    str_data[data_len+1] = '\0';
    printf("%s", str_data);
    return 0;
}

int print_block_inode(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private)
{
    printf("test cb inode %u, pos %lu, len %lu, path %s\n", inode, pos, data_len, path);
    return 0;
}

int noop_block(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private)
{
    return 0;
}

int scope_block(ext2_filsys fs, blk64_t *blocknr, e2_blkcnt_t blockcnt, blk64_t ref_blk, int ref_offset, void *private)
{
    struct scope_blocks_info *scope_info = (struct scope_blocks_info *)private;

    // ignore the extra "empty" block at the end of a file, to allow appending for writers, when we pass in BLOCK_FLAG_HOLE
    if (blockcnt * fs->blocksize >= scope_info->inode_info->len)
        return 0;

    struct block_list *list = ecalloc(sizeof(struct block_list));
    list->inode_info = scope_info->inode_info;
    list->inode_info->references++;
    list->physical_block = *blocknr;
    list->logical_block = blockcnt;

    // sparse files' hole blocks should be passed to this function, since we passed BLOCK_FLAG_HOLE to the
    // iterator function, but that doesn't seem to be happening - so fix it
    e2_blkcnt_t last_logical_block = scope_info->list_start == NULL ? -1 : scope_info->list_end->logical_block;
    blk64_t zero_physical_block = 0;
    for (e2_blkcnt_t i = last_logical_block+1; i < blockcnt; i++)
        scope_block(fs, &zero_physical_block, i, 0, 0, private);

    list->logical_block = blockcnt;

    //fprintf(stderr, "logical block %ld (%ld) is physical block %ld\n", list->logical_block, blockcnt, list->physical_block);

    if (scope_info->list_start == NULL)
    {
        scope_info->list_start = list;
        scope_info->list_end = list;
    }
    else
    {
        scope_info->list_end->next = list;
        scope_info->list_end = list;
    }
    return 0;
}

#define MAX_INODES 1
#define MAX_BLOCKS 128000

int compare_physical_blocks(struct block_list *p, struct block_list *q)
{
    return p->physical_block < q->physical_block ? -1 : 1;
}

int compare_logical_blocks(struct block_list *p, struct block_list *q)
{
    return p->logical_block < q->logical_block ? -1 : 1;
}

void iterate_dir(char *dev_path, char *target_path, block_cb cb)
{
    ext2_filsys fs;
    CHECK_FATAL(ext2fs_open(dev_path, 0, 0, 0, unix_io_manager, &fs),
            "while opening file system on device %s", dev_path);

    int fd;
    if ((fd = open(dev_path, O_RDONLY)) < 0)
        exit_str("Error opening block device %s", dev_path);

    ext2_ino_t ino;
    CHECK_FATAL(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, target_path, &ino),
            "while looking up path %s", target_path);

    struct dir_entry_cb_data cb_data = { fs, NULL, NULL, NULL };
    errcode_t dir_check_result = ext2fs_check_directory(fs, ino);
    if (dir_check_result == 0)
    {
        struct dir_tree_entry dir = { ino, target_path, NULL };
        cb_data.dir = &dir;
        CHECK_FATAL(ext2fs_dir_iterate2(fs, ino, 0, NULL, dir_entry_cb, &cb_data),
                "while iterating over directory %s", target_path);
    }
    else if (dir_check_result == EXT2_ET_NO_DIRECTORY)
    {
        int dir_path_len = strrchr(target_path, '/') - target_path;
        char *dir_path = malloc(dir_path_len+1);
        memcpy(dir_path, target_path, dir_path_len);;
        dir_path[dir_path_len] = '\0';

        struct dir_tree_entry dir = { ino, dir_path, NULL };
        cb_data.dir = &dir;
        dir_entry_add_file(ino, strrchr(target_path, '/')+1, &cb_data);
    }
    else
    {
        com_err(prog_name, dir_check_result, "while checking if inode %d is a directory", ino);
        exit(1);
    }

    struct inode_list *inode_list = inode_list_sort(cb_data.list_start);
    int open_inodes_count = 0;

    char block_buf[fs->blocksize * 3];
    struct scope_blocks_info scope_info = { cb, NULL, NULL, NULL };

    while (1)
    {
        while (inode_list != NULL && open_inodes_count < MAX_INODES)
        {
            char *path = malloc(strlen(inode_list->dir->path) + strlen(inode_list->name) + 2);
            sprintf(path, "%s/%s", inode_list->dir->path, inode_list->name);

            struct inode_cb_info *info = ecalloc(sizeof(struct inode_cb_info));
            info->inode = inode_list->index;
            info->path = path;
            info->len = inode_list->len;
            info->block_cache = NULL;
            info->blocks_read = 0;
            info->references = 0;

            scope_info.inode_info = info;

            //printf("path: %s\n", info->path);

            CHECK_WARN_DO(ext2fs_block_iterate3(fs, info->inode, BLOCK_FLAG_HOLE | BLOCK_FLAG_DATA_ONLY | BLOCK_FLAG_READ_ONLY, block_buf, scope_block, &scope_info), continue,
                    "while iterating over blocks of inode %d", info->inode);

            if (info->references == 0)
            {
                // empty files generate no blocks, so we'd get into an infinite loop below
                scope_info.cb(info->inode, info->path, 0, 0, NULL, 0, &info->cb_private);
                free(info->path);
                free(info);
            }
            else
                open_inodes_count++;

            inode_list = inode_list->next;
        }

        //scope_info.list_start = block_list_sort(scope_info.list_start);

        struct block_list *block_list = scope_info.list_start;
        scope_info.list_start = NULL;
        struct block_list **prev_next_ptr = &scope_info.list_start;

        int max_inode_blocks = (MAX_BLOCKS+open_inodes_count-1)/open_inodes_count;

        // for the holes in sparse files
        char zero_block[fs->blocksize];
        memset(zero_block, 0, fs->blocksize);

        while (block_list != NULL)
        {
            struct inode_cb_info *inode_info = block_list->inode_info;

            uint64_t pos = inode_info->blocks_read * ((uint64_t)fs->blocksize);
            uint64_t remaining_len = inode_info->len - pos;
            int read_len = remaining_len > fs->blocksize ? fs->blocksize : remaining_len;

            if (block_list->logical_block < inode_info->blocks_read + max_inode_blocks)
            {
                block_list->data = emalloc(read_len);
                if (block_list->physical_block != 0)
                {
                    off_t offset = block_list->physical_block * ((off_t)fs->blocksize);
                    if (lseek(fd, offset, SEEK_SET) == -1)
                        exit_str("Error seeking block device");

                    if (read(fd, block_list->data, read_len) != read_len)
                        exit_str("Error reading from block device");
                }
                else
                    memcpy(block_list->data, zero_block, read_len);

                if (inode_info->block_cache == NULL)
                    inode_info->block_cache = heap_create(max_inode_blocks);

                //printf("inode: %d\n", inode_info->inode);
                heap_insert(inode_info->block_cache, block_list->logical_block, block_list);

                //fprintf(stderr, "inserted block %ld into blocks heap for %s (%d references, %ld blocks read, min %ld): ", block_list->logical_block, inode_info->path, inode_info->references, inode_info->blocks_read, ((struct block_list *)heap_min(inode_info->block_cache))->logical_block);
                //print_heap(stderr, inode_info->block_cache);
                //verify_heap(inode_info->block_cache);

                // block_list could be freed if it's the heap minimum, so iterate to the next block here
                block_list = block_list->next;

                while (heap_size(inode_info->block_cache) > 0 && ((struct block_list *)heap_min(inode_info->block_cache))->logical_block == inode_info->blocks_read)
                {
                    if (inode_info->references <= 0)
                        exit_str("inode %d has %d references\n", inode_info->inode, inode_info->references);

                    struct block_list *next_block = heap_delmin(inode_info->block_cache);
                    
                    //fprintf(stderr, "removed from blocks heap for %s (%d references): ", inode_info->path, inode_info->references);
                    //print_heap(stderr, inode_info->block_cache);
                    //verify_heap(inode_info->block_cache);

                    uint64_t next_block_pos = inode_info->blocks_read * ((uint64_t)fs->blocksize);
                    uint64_t next_block_remaining_len = inode_info->len - next_block_pos;
                    int next_block_read_len = next_block_remaining_len > fs->blocksize ? fs->blocksize : next_block_remaining_len;
                    //printf("inode_info->len: %lu, next_block_pos: %lu, next_block_remaining_len: %lu, next_block_read_len: %d\n", inode_info->len, next_block_pos, next_block_remaining_len, next_block_read_len);
                    scope_info.cb(inode_info->inode, inode_info->path, pos, inode_info->len, next_block->data, next_block_read_len, &inode_info->cb_private);

                    // blocks_read is used in file pos calculation; make sure to only increment afterward
                    inode_info->blocks_read++;

                    free(next_block->data);
                    free(next_block);

                    if (--inode_info->references == 0)
                    {
                        if (inode_info->block_cache != NULL)
                            heap_destroy(inode_info->block_cache);
                        free(inode_info->path);
                        free(inode_info);
                        open_inodes_count--;
                    }
                }
            }
            else
            {
                fprintf(stderr, "block %ld (%ld blocks read; %u max blocks) for %s out of range\n", block_list->logical_block, inode_info->blocks_read, max_inode_blocks, inode_info->path);
                struct block_list *old_next = block_list->next;
                *(prev_next_ptr) = block_list;
                prev_next_ptr = &block_list->next;
                block_list->next = NULL;

                block_list = old_next;
            }

        }

        if (inode_list == NULL)
            break;
        //else
            //printf("remaining inode name: %s\n", inode_list->name);
    }

    if (close(fd) != 0)
        exit_str("Error closing block device");

    if (ext2fs_close(fs) != 0)
        exit_str("Error closing file system");
}

void initialize_ext_batching(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
    read_buf = malloc(read_buf_len);
}

int main2(int argc, char **argv)
{
    initialize_ext_batching(argv[0]);
    iterate_dir(argv[1], argv[2], noop_block);
    return 0;
}
