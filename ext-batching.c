#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <alloca.h>
#include <stdio.h>
#include <stdint.h>
#include <et/com_err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>


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
    char *path;
    uint64_t len;
    struct inode_list *next;

    struct block_list *blocks_start;
    struct block_list *blocks_end;
};

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

struct inode_cb_info
{
    ext2_ino_t inode;
    char *path;
    uint64_t len;
    e2_blkcnt_t blocks_read;
    e2_blkcnt_t blocks_scanned;
    struct heap *block_cache;
    void *cb_private;
    int references;
};

struct referenced_data
{
    char *data;
    e2_blkcnt_t references;
};

struct data_reference
{
    struct referenced_data *ref;
    off_t pos;
    size_t len;
};

struct block_list
{
    struct inode_cb_info *inode_info;
    blk64_t physical_block;
    e2_blkcnt_t logical_block;
    struct data_reference data_ref;
    struct block_list *next;
};

struct scan_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct inode_list *inode_list;
    struct block_list *list_start;
    struct block_list *list_end;
};

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
        path = malloc(strlen(dir->path) + strlen(name) + 2);
        sprintf(path, "%s/%s", dir->path, name);
    }
    return path;
}

void dir_entry_add_file(ext2_ino_t ino, char *name, struct dir_entry_cb_data *cb_data, uint64_t len)
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

int dir_entry_cb(ext2_ino_t dir_ino, int entry, struct ext2_dir_entry *dirent, int offset, int blocksize, char *buf, void *private)
{
    // saw the 0xFF in e2fsprogs sources; don't know why the high bits would be set
    int name_len = dirent->name_len & 0xFF;
    char name[name_len+1];
    memcpy(name, dirent->name, name_len);
    name[name_len] = '\0';

    if (entry == DIRENT_OTHER_FILE)
    {
        struct dir_entry_cb_data *cb_data = (struct dir_entry_cb_data *)private;

        struct ext2_inode inode_contents;
        CHECK_FATAL(ext2fs_read_inode(cb_data->fs, dirent->inode, &inode_contents),
                "while reading inode contents");
        if (LINUX_S_ISDIR(inode_contents.i_mode))
        {
            //fprintf(stderr, "directory dir: %s, name: %s\n", cb_data->dir->path, name);
            struct dir_tree_entry dir;
            dir.parent = cb_data->dir;
            dir.path = dir_path_append_name(dir.parent, name);

            char block_buf[cb_data->fs->blocksize*3];

            cb_data->dir = &dir;
            CHECK_FATAL(ext2fs_dir_iterate2(cb_data->fs, dirent->inode, 0, block_buf, dir_entry_cb, cb_data),
                    "while iterating over directory %s", name);
            cb_data->dir = cb_data->dir->parent;

            free(dir.path);
        }
        else if (!S_ISLNK(inode_contents.i_mode))
        {
            //fprintf(stderr, "file dir: %s, name: %s\n", cb_data->dir->path, name);
            dir_entry_add_file(dirent->inode, name, cb_data, inode_contents.i_size);
        }
    }

    return 0;
}

// inode indexes are unsigned ints, so don't subtract them
SORT_FUNC(inode_list_sort, struct inode_list, (p->index < q->index ? -1 : 1))
SORT_FUNC(block_list_sort, struct block_list, (p->physical_block < q->physical_block ? -1 : 1))

int scan_block(ext2_filsys fs, blk64_t *blocknr, e2_blkcnt_t blockcnt, blk64_t ref_blk, int ref_offset, void *private)
{
    struct scan_blocks_info *scan_info = (struct scan_blocks_info *)private;
    //if (blockcnt == 0)
    //    fprintf(stderr, "first of %s\n", scan_info->inode_info->path);

    // Ignore the extra "empty" block at the end of a file, to allow appending for writers, when we pass in BLOCK_FLAG_HOLE,
    // unless the file is empty
    if (blockcnt * fs->blocksize >= scan_info->inode_info->len || scan_info->inode_info->len == 0)
        return 0;

    //fprintf(stderr, "logical block %ld of %s is physical block %ld\n", blockcnt, scan_info->inode_info->path, *blocknr);
    struct block_list *list = ecalloc(sizeof(struct block_list));
    list->inode_info = scan_info->inode_info;
    list->inode_info->references++;
    list->physical_block = *blocknr;
    list->logical_block = blockcnt;

    uint64_t logical_pos = list->logical_block * ((uint64_t)fs->blocksize);
    uint64_t remaining_len = list->inode_info->len - logical_pos;
    list->data_ref.len = remaining_len > fs->blocksize ? fs->blocksize : remaining_len;

    // sparse files' hole blocks should be passed to this function, since we passed BLOCK_FLAG_HOLE to the
    // iterator function, but that doesn't seem to be happening - so fix it
    blk64_t zero_physical_block = 0;
    for (e2_blkcnt_t i = list->inode_info->blocks_scanned; i < blockcnt; i++)
        scan_block(fs, &zero_physical_block, i, 0, 0, private);

    list->inode_info->blocks_scanned++;

    struct inode_list *inode_list = scan_info->inode_list;
    if (inode_list->blocks_start == NULL)
    {
        inode_list->blocks_start = list;
        inode_list->blocks_end = list;
    }
    else
    {
        inode_list->blocks_end->next = list;
        inode_list->blocks_end = list;
    }
    return 0;
}

void flush_inode_blocks(ext2_filsys fs, struct inode_cb_info *inode_info, block_cb cb, int *open_inodes_count)
{
    //fprintf(stderr, "heap size: %d, min: %d, blocks_read: %d\n", heap_size(inode_info->block_cache), ((struct block_list *)heap_min(inode_info->block_cache))->logical_block, inode_info->blocks_read);
    while (heap_size(inode_info->block_cache) > 0 && ((struct block_list *)heap_min(inode_info->block_cache))->logical_block == inode_info->blocks_read)
    {
        if (inode_info->references <= 0)
            exit_str("inode %d has %d references\n", inode_info->path, inode_info->references);

        //fprintf(stderr, "removing min %ld\n", ((struct block_list *)heap_min(inode_info->block_cache))->logical_block);
        struct block_list *next_block = heap_delmin(inode_info->block_cache);
        
        //fprintf(stderr, "removed from blocks heap for %s (%d references): ", inode_info->path, inode_info->references);
        //print_heap(stderr, inode_info->block_cache);
        //verify_heap(inode_info->block_cache);
        //fprintf(stderr, "\n");

        //fprintf(stderr, "buffer %p has %ld references\n", next_block->data_ref.ref, next_block->data_ref.ref->references);

        uint64_t logical_pos = next_block->logical_block * ((uint64_t)fs->blocksize);
        char *block_data = next_block->data_ref.ref->data + next_block->data_ref.pos;
        cb(inode_info->inode, inode_info->path, logical_pos, inode_info->len, block_data, next_block->data_ref.len, &inode_info->cb_private);

        inode_info->blocks_read++;

        if (--next_block->data_ref.ref->references == 0)
        {
            //fprintf(stderr, "freeing buffer of %p at %p\n", next_block->data_ref.ref, next_block->data_ref.ref->data);
            free(next_block->data_ref.ref->data);
            free(next_block->data_ref.ref);
        }

        free(next_block);

        //fprintf(stderr, "references to %s: %d\n", inode_info->path, inode_info->references-1);
        if (--inode_info->references == 0)
        {
            if (inode_info->block_cache != NULL)
                heap_destroy(inode_info->block_cache);
            free(inode_info->path);
            free(inode_info);
            (*open_inodes_count)--;
            break;
        }
        //fprintf(stderr, "open_inodes_count: %d\n", *open_inodes_count);
    }
}

void iterate_dir(char *dev_path, char *target_path, block_cb cb, int max_inodes, int max_blocks, int coalesce_distance, int flags)
{
    ext2_filsys fs;
    CHECK_FATAL(ext2fs_open(dev_path, 0, 0, 0, unix_io_manager, &fs),
            "while opening file system on device %s", dev_path);

    int fd;
    int open_flags = O_RDONLY;
    if (flags & ITERATE_OPT_DIRECT)
        open_flags |= O_DIRECT;
    if ((fd = open(dev_path, open_flags)) < 0)
        exit_str("Error opening block device %s", dev_path);

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "BEGIN INODE SCAN\n");

    ext2_ino_t ino;
    CHECK_FATAL(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, target_path, &ino),
            "while looking up path %s", target_path);

    // find the inodes we want to read
    struct dir_entry_cb_data cb_data = { fs, NULL, NULL, NULL };
    struct ext2_inode inode_contents;
    CHECK_FATAL(ext2fs_read_inode(fs, ino, &inode_contents),
            "while reading inode contents");
    if (LINUX_S_ISDIR(inode_contents.i_mode))
    {
        // if it's a directory, iterate over it
        struct dir_tree_entry dir = { target_path, NULL };
        cb_data.dir = &dir;
        CHECK_FATAL(ext2fs_dir_iterate2(fs, ino, 0, NULL, dir_entry_cb, &cb_data),
                "while iterating over directory %s", target_path);
    }
    else if (!S_ISLNK(inode_contents.i_mode))
    {
        // if it's a regular file, just add it
        int dir_path_len = strrchr(target_path, '/') - target_path;
        char *dir_path = malloc(dir_path_len+1);
        memcpy(dir_path, target_path, dir_path_len);;
        dir_path[dir_path_len] = '\0';

        struct dir_tree_entry dir = { dir_path, NULL };
        cb_data.dir = &dir;
        dir_entry_add_file(ino, strrchr(target_path, '/')+1, &cb_data, inode_contents.i_size);
    }
    else
        exit_str("Unexpected file mode %x", inode_contents.i_mode);

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "END INODE SCAN\n");

    struct inode_list *inode_list = inode_list_sort(cb_data.list_start);
    int open_inodes_count = 0;
    uint64_t seeks = 0;
    uint64_t total_blocks = 0;

    char block_buf[fs->blocksize * 3];
    struct scan_blocks_info scan_info = { cb, NULL, NULL, NULL, NULL };

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "BEGIN BLOCK SCAN\n");

    struct inode_list *scan_inode_list = inode_list;
    while (scan_inode_list != NULL && open_inodes_count < max_inodes)
    {
        struct inode_cb_info *info = ecalloc(sizeof(struct inode_cb_info));
        info->inode = scan_inode_list->index;
        info->path = malloc(strlen(scan_inode_list->path)+1);
        strcpy(info->path, scan_inode_list->path);
        info->len = scan_inode_list->len;

        scan_info.inode_info = info;
        scan_info.inode_list = scan_inode_list;

        //fprintf(stderr, "scoping blocks of path: %s\n", info->path);

        CHECK_FATAL(ext2fs_block_iterate3(fs, info->inode, BLOCK_FLAG_HOLE | BLOCK_FLAG_DATA_ONLY | BLOCK_FLAG_READ_ONLY, block_buf, scan_block, &scan_info),
                "while iterating over blocks of inode %d", info->inode);

        if (info->references == 0)
        {
            // empty files generate no blocks, so we'd get into an infinite loop below
            //fprintf(stderr, "empty %s\n", info->path);
            scan_info.cb(info->inode, info->path, 0, 0, NULL, 0, &info->cb_private);
            free(info->path);
            free(info);
        }

        scan_inode_list = scan_inode_list->next;
    }

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "END BLOCK SCAN\n");

    while (1)
    {
        while (inode_list != NULL && open_inodes_count < max_inodes)
        {
            if (inode_list->blocks_start != NULL)
            {
                if (scan_info.list_start == NULL)
                {
                    scan_info.list_start = inode_list->blocks_start;
                    scan_info.list_end = inode_list->blocks_end;
                }
                else
                {
                    scan_info.list_end->next = inode_list->blocks_start;
                    scan_info.list_end = inode_list->blocks_end;
                }
                open_inodes_count++;
            }

            struct inode_list *old = inode_list;
            inode_list = inode_list->next;

            free(old->path);
            free(old);
        }

        scan_info.list_start = block_list_sort(scan_info.list_start);

        struct block_list *block_list = scan_info.list_start;
        scan_info.list_start = NULL;
        struct block_list **prev_next_ptr = &scan_info.list_start;

        int max_inode_blocks = open_inodes_count > 0 ? (max_blocks+open_inodes_count-1)/open_inodes_count : max_blocks;
        //fprintf(stderr, "max_inode_blocks: %d\n", max_inode_blocks);

        if (flags & ITERATE_OPT_PROFILE)
            fprintf(stderr, "BEGIN BLOCK READ\n");
        while (block_list != NULL)
        {
            total_blocks++;
            struct referenced_data *ref = ecalloc(sizeof(struct referenced_data));

            e2_blkcnt_t consecutive_blocks = 0;
            size_t consecutive_len = 0;
            struct block_list *fwd_block_list = block_list;
            struct block_list *prev_fwd_block = NULL;
            while (1)
            {
                //fprintf(stderr, "%ld < %ld + %d = %s\n", fwd_block_list->logical_block, fwd_block_list->inode_info->blocks_read, max_inode_blocks,
                //        fwd_block_list->logical_block < fwd_block_list->inode_info->blocks_read + max_inode_blocks ? "true" : "false");
                if (fwd_block_list->logical_block >= fwd_block_list->inode_info->blocks_read + max_inode_blocks)
                    break;

                ref->references++;

                consecutive_blocks++;
                e2_blkcnt_t physical_block_diff = prev_fwd_block == NULL ? 0 : fwd_block_list->physical_block - prev_fwd_block->physical_block;
                if (prev_fwd_block != NULL)
                    consecutive_len += (physical_block_diff - 1) * ((uint64_t)fs->blocksize);

                /*if (physical_block_diff > 1)
                    fprintf(stderr, "skipping %ld blocks between %ld and %ld of %s\n", physical_block_diff,
                            prev_fwd_block->physical_block, fwd_block_list->physical_block, fwd_block_list->inode_info->path);*/

                fwd_block_list->data_ref.pos = (fwd_block_list->physical_block - block_list->physical_block) * ((uint64_t)fs->blocksize);

                if (fwd_block_list->data_ref.len != fs->blocksize
                        || fwd_block_list->next == NULL
                        || (fwd_block_list->next->physical_block > fwd_block_list->physical_block+coalesce_distance
                            && !(fwd_block_list->physical_block == 0 && fwd_block_list->next->physical_block == 0)))
                {
                    consecutive_len += fwd_block_list->data_ref.len;
                    break;
                }
                else
                    consecutive_len += fs->blocksize;

                prev_fwd_block = fwd_block_list;
                fwd_block_list = fwd_block_list->next;
            }

            //fprintf(stderr, "consecutive len: %lu, blocks: %ld\n", consecutive_len, consecutive_blocks);

            if (consecutive_blocks > 0)
            {
                if (block_list->physical_block != 0)
                {
                    // If opened with O_DIRECT, the device needs to be read in multiples of 512 bytes into a buffer that
                    // is 512-byte aligned. Only the latter is documented; the former is documented as being undocumented.
                    size_t physical_read_len = flags & ITERATE_OPT_DIRECT ? ((consecutive_len+511)/512)*512 : consecutive_len;
                    if (posix_memalign((void **)&ref->data, 512, physical_read_len))
                        perror("Error allocating aligned memory");

                    off_t physical_pos = block_list->physical_block * ((off_t)fs->blocksize);
                    if (pread(fd, ref->data, physical_read_len, physical_pos) < consecutive_len)
                        perror("Error reading from block device");
                    seeks++;
                }
                else
                    ref->data = ecalloc(consecutive_len);
            }
            else
                free(ref);

            //fprintf(stderr, "allocated buffer of %p at %p\n", ref, ref->data);

            e2_blkcnt_t read_blocks = 0;
            while (read_blocks < consecutive_blocks)
            {
                block_list->data_ref.ref = ref;
                read_blocks++;

                struct inode_cb_info *inode_info = block_list->inode_info;
                if (inode_info->block_cache == NULL)
                    inode_info->block_cache = heap_create(max_inode_blocks);

                //fprintf(stderr, "inserting %ld\n", block_list->logical_block);
                heap_insert(inode_info->block_cache, block_list->logical_block, block_list);

                /*fprintf(stderr, "inserted block %ld into blocks heap for %s (%d references, %ld blocks read, min %ld): ", block_list->logical_block, inode_info->path, inode_info->references, inode_info->blocks_read, ((struct block_list *)heap_min(inode_info->block_cache))->logical_block);
                print_heap(stderr, inode_info->block_cache);
                verify_heap(inode_info->block_cache);*/

                // block_list could be freed if it's the heap minimum, so iterate to the next block before flushing cached blocks
                block_list = block_list->next;
                flush_inode_blocks(fs, inode_info, cb, &open_inodes_count);
            }

            // block is out of range
            if (consecutive_len == 0)
            {
                //fprintf(stderr, "block %ld (%ld blocks read; %u max blocks) for %s out of range\n", block_list->logical_block, inode_info->blocks_read, max_inode_blocks, inode_info->path);
                struct block_list *old_next = block_list->next;
                *(prev_next_ptr) = block_list;
                scan_info.list_end = block_list;
                prev_next_ptr = &block_list->next;
                block_list->next = NULL;

                block_list = old_next;
            }
        }
        if (flags & ITERATE_OPT_PROFILE)
            fprintf(stderr, "END BLOCK READ\n");

        if (inode_list == NULL)
            break;
        //else
            //printf("remaining inode name: %s\n", inode_list->name);
    }

    double seeks_percentage = total_blocks == 0 ? 0. : ((double)seeks)/((double)total_blocks) * 100.;
    //fprintf(stderr, "seeks/blocks = %lu/%lu = %lf%%\n", seeks, total_blocks, seeks_percentage);

    if (close(fd) != 0)
        exit_str("Error closing block device");

    if (ext2fs_close(fs) != 0)
        exit_str("Error closing file system");
}

void initialize_ext_batching(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
}
