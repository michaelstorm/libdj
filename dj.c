#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <alloca.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "crc.h"
#include "dir_scan.h"
#include "dj.h"
#include "dj_internal.h"
#include "heap.h"
#include "listsort.h"
#include "util.h"

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

struct scan_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct inode_list *inode_list;
};

// inode indexes are unsigned ints, so don't subtract them
SORT_FUNC(inode_list_sort, struct inode_list, (p->index < q->index ? -1 : 1)); // semicolons seem to help Sublime Text's syntax parser
SORT_FUNC(block_list_sort, struct block_list, (p->physical_block < q->physical_block ? -1 : 1));

/*
 * Callback invoked by libext2fs for each block of a file.
 * - Increments the reference count of the block's inode.
 * - Sets the block's metadata (such as logical and physical numbers and length)
 *   in a block_list struct.
 * - Inserts the block_list struct into the inode's linked list of its blocks.
 * - Recursively calls itself to add blocks for holes.
 */
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
    list->stripe_ptr.len = remaining_len > fs->blocksize ? fs->blocksize : remaining_len;

    // sparse files' hole blocks should be passed to this function, since we passed BLOCK_FLAG_HOLE to the
    // iterator function, but that doesn't seem to be happening - so fix it
    blk64_t zero_physical_block = 0; // TODO can be static
    // FIXME will this produce an infinite recursion when there's more than one
    // contiguous hole? What happens when holes are at the end of the file?
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

        //fprintf(stderr, "buffer %p has %ld references\n", next_block->stripe_ptr.stripe, next_block->stripe_ptr.stripe->references);

        uint64_t logical_pos = next_block->logical_block * ((uint64_t)fs->blocksize);
        char *block_data = next_block->stripe_ptr.stripe->data + next_block->stripe_ptr.pos;
        cb(inode_info->inode, inode_info->path, logical_pos, inode_info->len, block_data, next_block->stripe_ptr.len, &inode_info->cb_private);

        inode_info->blocks_read++;

        if (--next_block->stripe_ptr.stripe->references == 0)
        {
            //fprintf(stderr, "freeing buffer of %p at %p\n", next_block->stripe_ptr.stripe, next_block->stripe_ptr.stripe->data);
            free(next_block->stripe_ptr.stripe->data);
            free(next_block->stripe_ptr.stripe);
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
    // open file system from block device
    ext2_filsys fs;
    CHECK_FATAL(ext2fs_open(dev_path, 0, 0, 0, unix_io_manager, &fs),
            "while opening file system on device %s", dev_path);

    // open the block device in order to read data from it later
    int fd;
    int open_flags = O_RDONLY;
    if (flags & ITERATE_OPT_DIRECT)
        open_flags |= O_DIRECT;
    if ((fd = open(dev_path, open_flags)) < 0)
        exit_str("Error opening block device %s", dev_path);

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "BEGIN INODE SCAN\n");

    // look up the file whose blocks we want to read, or the directory whose
    // constituent files (and their block) we want to read
    ext2_ino_t ino;
    CHECK_FATAL(ext2fs_namei_follow(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, target_path, &ino),
            "while looking up path %s", target_path);

    // get that inode
    struct dir_entry_cb_data cb_data = { fs, NULL, NULL, NULL };
    struct ext2_inode inode_contents;
    CHECK_FATAL(ext2fs_read_inode(fs, ino, &inode_contents),
            "while reading inode contents");

    if (LINUX_S_ISDIR(inode_contents.i_mode))
    {
        // if it's a directory, recursively iterate through its contents
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

    /*
     * We now have a linked list of file paths to be scanned in cb_data.list_start.
     */

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "END INODE SCAN\n");

    /*
     * You may ask yourself, "Why sort on the inode numbers at all, if what we
     * really care about is the order of the data blocks?" My friend, you ask
     * a reasonable question. Consider the following.
     *
     *   a) We can only keep a certain number of blocks in memory at a time.
     *
     *   b) We can only keep a certain number of files in memory at a time.
     *      This is mostly due to clients that maintain state on a per-file
     *      basis, which is an entirely reasonable thing to do. If we feed them
     *      too many files at once, they blow out all their memory.
     *
     *   c) Therefore, we have to choose which files to buffer in memory at any
     *      one time. We can do this:
     *      i) intelligently, by iterating through the files' metadata and
     *         figuring out which sets of files have the most-contiguous
     *         blocks, or
     *      ii) stupidly, by sorting the files on their inode numbers and
     *         working our way through them, on the assumption that the
     *         blocks of close-together inodes will be close together
     *         themselves.
     *
     * I have chosen option (ii), for the moment. You may ask yourself, "Doesn't
     * that merely punt on the problem we're trying to solve? This whole
     * exercise about sorting blocks is predicated on the file system not
     * allocating blocks perfectly efficiently, but now we're going to assume
     * that the ordering of inodes is reasonably close to the ordering of their
     * blocks?"
     *
     * Yes. Because I'm too lazy at the moment to do any better (yay, open
     * source), and because a perfect implementation of option (i) doesn't
     * exist in the general case, *assuming* that such an implementation
     * requires non-constant (and non-trivial) memory asymptotically. In other
     * words, if we have a really low constraint on available memory and a
     * really large file system to examine, there ain't no way to pick sets of
     * files with mostly-contiguous blocks without eating up all your memory
     * tracking where those blocks are. At least, I think this is the case.
     *
     * Mind you, this is a *perfect* solution we're talking about being
     * impossible within memory contraints. Sorting on inodes is just a
     * heuristic, and there are others to choose from.
     */

    struct inode_list *inode_list = inode_list_sort(cb_data.list_start);
    int open_inodes_count = 0;
    uint64_t seeks = 0;
    uint64_t total_blocks = 0;

    char block_buf[fs->blocksize * 3];
    struct scan_blocks_info scan_info = { cb, NULL, NULL };

    struct block_list *block_list_start;
    struct block_list *block_list_end;

    if (flags & ITERATE_OPT_PROFILE)
        fprintf(stderr, "BEGIN BLOCK SCAN\n");

    /*
     * For each inode, add the metadata for each of that inode's blocks to the
     * inode's block list.
     */
    struct inode_list *scan_inode_list = inode_list;
    while (scan_inode_list != NULL)
    {
        struct inode_cb_info *info = ecalloc(sizeof(struct inode_cb_info));
        info->inode = scan_inode_list->index;
        info->path = malloc(strlen(scan_inode_list->path)+1);
        strcpy(info->path, scan_inode_list->path);
        info->len = scan_inode_list->len;

        // there's some duplication of information (path and len) between
        // scan_info.inode_info and .inode_list, but that's ok
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
        /*
         * While there are inodes remaining and we're below the limit on open
         * inodes, add those inodes' blocks to the global list.
         */
        while (inode_list != NULL && open_inodes_count < max_inodes)
        {
            if (inode_list->blocks_start != NULL)
            {
                if (block_list_start == NULL)
                {
                    block_list_start = inode_list->blocks_start;
                    block_list_end = inode_list->blocks_end;
                }
                else
                {
                    block_list_end->next = inode_list->blocks_start;
                    block_list_end = inode_list->blocks_end;
                }
                open_inodes_count++;
            }

            struct inode_list *old = inode_list;
            inode_list = inode_list->next;

            free(old->path);
            free(old);
        }

        // sort the blocks into the order in which they're laid out on disk
        block_list_start = block_list_sort(block_list_start);

        struct block_list *block_list = block_list_start;
        block_list_start = NULL;
        struct block_list **prev_next_ptr = &block_list_start;

        int max_inode_blocks = open_inodes_count > 0 ? (max_blocks+open_inodes_count-1)/open_inodes_count : max_blocks;
        //fprintf(stderr, "max_inode_blocks: %d\n", max_inode_blocks);

        if (flags & ITERATE_OPT_PROFILE)
            fprintf(stderr, "BEGIN BLOCK READ\n");

        while (block_list != NULL)
        {
            total_blocks++;
            struct stripe *stripe = ecalloc(sizeof(struct stripe));

            // total number of consecutive blocks, excluding gaps
            e2_blkcnt_t consecutive_blocks = 0;

            // total length of consecutive blocks in bytes, including gaps
            size_t consecutive_len = 0;

            struct block_list *fwd_block_list = block_list;
            struct block_list *prev_fwd_block = NULL;
            while (1)
            {
                //fprintf(stderr, "%ld < %ld + %d = %s\n", fwd_block_list->logical_block, fwd_block_list->inode_info->blocks_read, max_inode_blocks,
                //        fwd_block_list->logical_block < fwd_block_list->inode_info->blocks_read + max_inode_blocks ? "true" : "false");
                if (fwd_block_list->logical_block >= fwd_block_list->inode_info->blocks_read + max_inode_blocks)
                    break;

                fwd_block_list->stripe_ptr.stripe = stripe;
                stripe->references++;

                consecutive_blocks++;
                e2_blkcnt_t physical_block_diff = prev_fwd_block == NULL ? 0 : fwd_block_list->physical_block - prev_fwd_block->physical_block;
                if (prev_fwd_block != NULL)
                    consecutive_len += (physical_block_diff - 1) * ((uint64_t)fs->blocksize); // TODO why -1?

                /*if (physical_block_diff > 1)
                    fprintf(stderr, "skipping %ld blocks between %ld and %ld of %s\n", physical_block_diff,
                            prev_fwd_block->physical_block, fwd_block_list->physical_block, fwd_block_list->inode_info->path);*/

                // set the block's start point relative to the stripe
                fwd_block_list->stripe_ptr.pos = (fwd_block_list->physical_block - block_list->physical_block) * ((uint64_t)fs->blocksize);
                consecutive_len += fwd_block_list->stripe_ptr.len;

                prev_fwd_block = fwd_block_list;
                fwd_block_list = fwd_block_list->next;
            }

            //fprintf(stderr, "consecutive len: %lu, blocks: %ld\n", consecutive_len, consecutive_blocks);

            /*
             * Read data from device into stripe.
             */
            if (consecutive_blocks > 0)
            {
                if (block_list->physical_block != 0)
                {
                    // If opened with O_DIRECT, the device needs to be read in multiples of 512 bytes into a buffer that
                    // is 512-byte aligned. Only the latter is documented; the former is documented as being undocumented.
                    size_t physical_read_len = flags & ITERATE_OPT_DIRECT ? ((consecutive_len+511)/512)*512 : consecutive_len;
                    if (posix_memalign((void **)&stripe->data, 512, physical_read_len))
                        perror("Error allocating aligned memory");

                    off_t physical_pos = block_list->physical_block * ((off_t)fs->blocksize);
                    if (pread(fd, stripe->data, physical_read_len, physical_pos) < consecutive_len)
                        perror("Error reading from block device");
                    seeks++;
                }
                else
                    stripe->data = ecalloc(consecutive_len);
            }
            else
                free(stripe);

            //fprintf(stderr, "allocated buffer of %p at %p\n", ref, ref->data);

            e2_blkcnt_t read_blocks = 0;
            while (read_blocks < consecutive_blocks)
            {
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
                block_list_end = block_list;
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

void initialize_dj(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
}
