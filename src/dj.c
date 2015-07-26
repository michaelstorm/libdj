#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "block_scan.h"
#include "clog.h"
#include "dir_scan.h"
#include "dj_internal.h"
#include "heap.h"
#include "listsort.h"
#include "util.h"

// inode indexes are unsigned ints, so don't subtract them; semicolons seem to
// help Sublime Text's syntax parser
SORT_FUNC(inode_list_sort, struct inode_list, (p->index < q->index ? -1 : 1));
SORT_FUNC(block_list_sort, struct block_list,
          (p->physical_block < q->physical_block ? -1 : 1));

void flush_inode_blocks(uint64_t block_size, struct inode_cb_info *inode_info,
                        block_cb cb, int *open_inodes_count)
{
    while (heap_size(inode_info->block_cache) > 0)
    {
        struct block_list *next_block = heap_min(inode_info->block_cache);
        if (next_block->logical_block == inode_info->blocks_read)
            heap_delmin(inode_info->block_cache);
        else
            break;

        if (inode_info->references <= 0)
        {
            exit_str("inode %d has %d references\n", inode_info->path,
                     inode_info->references);
        }
        
        uint64_t logical_pos = next_block->logical_block * block_size;
        char *block_data =
            next_block->stripe_ptr.stripe->data + next_block->stripe_ptr.pos;
        cb(inode_info->inode, inode_info->path, logical_pos, inode_info->len,
           block_data, next_block->stripe_ptr.len, &inode_info->cb_private);

        inode_info->blocks_read += next_block->num_blocks;

        if (--next_block->stripe_ptr.stripe->references == 0)
        {
            free(next_block->stripe_ptr.stripe->data);
            free(next_block->stripe_ptr.stripe);
        }

        free(next_block);

        if (--inode_info->references == 0)
        {
            if (inode_info->block_cache != NULL)
                heap_destroy(inode_info->block_cache);
            free(inode_info->path);
            free(inode_info);
            (*open_inodes_count)--;
            break;
        }
    }
}

/*
 * Read ahead of the current block (block_list) to determine the longest stripe
 * we can read all in one go, that satisfies the following conditions.
 *   1) The number of cached blocks in the heap of the inode of any block in the
 *      stripe is not greater than max_inode_blocks.
 *   2) The physical distance between any two blocks in the stripe that we care
 *      about (i.e., the ones that will be passed to the callback) is not
 *      greater than coalesce_distance.
 */
struct stripe *next_stripe(uint64_t block_size, int coalesce_distance,
                           int max_inode_blocks, struct block_list *block_list)
{
    struct stripe *stripe = ecalloc(sizeof(struct stripe));

    // we use this pointer to read ahead of the current block without losing our
    // place in the overall iteration
    struct block_list *fwd_block_list = block_list;

    // and this awfully-named number tracks the previous value of
    // fwd_block_list, in order to track how far we're jumping over blocks we
    // don't care about, so as not to exceed coalesce_distance
    struct block_list *prev_fwd_block = NULL;

    while (fwd_block_list != NULL)
    {
        // check condition (1)
        /*e2_blkcnt_t max_logical_block =
            fwd_block_list->inode_info->blocks_read + max_inode_blocks - 1;
        if (fwd_block_list->logical_block > max_logical_block)
            break;*/

        // check condition (2)
        e2_blkcnt_t physical_block_diff = prev_fwd_block == NULL
            ? 0
            : fwd_block_list->physical_block - (prev_fwd_block->physical_block + prev_fwd_block->num_blocks);
        if (physical_block_diff > coalesce_distance)
            break;

        stripe->consecutive_blocks += fwd_block_list->num_blocks;

        fwd_block_list->stripe_ptr.stripe = stripe;
        stripe->references++;

        // set the block's start point relative to the stripe
        blk64_t physical_block_offset =
            fwd_block_list->physical_block - block_list->physical_block;
        fwd_block_list->stripe_ptr.pos = physical_block_offset * block_size;

        stripe->consecutive_len += fwd_block_list->num_blocks * block_size; // actual block length
        stripe->consecutive_len += physical_block_diff * block_size; // gap between blocks

        prev_fwd_block = fwd_block_list;
        fwd_block_list = fwd_block_list->next;
    }

    return stripe;
}

/*
 * Read data from device into stripe.
 */
void read_stripe_data(off_t block_size, blk64_t physical_block, int direct,
                      int fd, struct stripe *stripe)
{
    if (physical_block != 0)
    {
        // If opened with O_DIRECT, the device needs to be read in multiples of
        // 512 bytes into a buffer that is 512-byte aligned. Only the latter is
        // documented; the former is documented as being undocumented.
        size_t physical_read_len = direct
            ? ((stripe->consecutive_len+511)/512)*512
            : stripe->consecutive_len;

        if (posix_memalign((void **)&stripe->data, 512, physical_read_len))
            perror("Error allocating aligned memory");

        ssize_t read_len = pread(fd, stripe->data, physical_read_len,
                                 physical_block * block_size);
        if (read_len < stripe->consecutive_len)
            perror("Error reading from block device");
    }
    else
        stripe->data = ecalloc(stripe->consecutive_len);
}

/*
 * For each block in the stripe, insert the block into its inode's heap. Then
 * flush that heap out to the client, if possible.
 */
struct block_list *heapify_stripe(ext2_filsys fs, block_cb cb,
                                  struct block_list *block_list,
                                  struct stripe *stripe, int max_inode_blocks,
                                  int *open_inodes_count)
{
    e2_blkcnt_t consecutive_blocks = stripe->consecutive_blocks; // stripe can be freed during iteration, so save the number of blocks here
    for (e2_blkcnt_t read_blocks = 0; read_blocks < consecutive_blocks;)
    {
        struct inode_cb_info *inode_info = block_list->inode_info;
        if (inode_info->block_cache == NULL)
            inode_info->block_cache = heap_create(inode_info->len/fs->blocksize+1 /*max_inode_blocks*/); // +1 so that it's never 0

        LogTrace("Heapifying physical block %lu, logical block %lu (num blocks %lu) of inode %d", block_list->physical_block, block_list->logical_block, block_list->num_blocks, inode_info->inode);
        heap_insert(inode_info->block_cache, block_list->logical_block,
                    block_list);

        read_blocks += block_list->num_blocks;

        // block_list could be freed if it's the heap minimum, so iterate to the
        // next block before flushing cached blocks
        block_list = block_list->next;
        flush_inode_blocks(fs->blocksize, inode_info, cb, open_inodes_count);
    }

    return block_list;
}

void iterate_dir(char *dev_path, char *target_path, block_cb cb, int max_inodes,
                 int max_blocks, int coalesce_distance, int flags, int advice_flags)
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

    CHECK_WARN(posix_fadvise(fd, 0, 0, advice_flags), "setting advice flags 0x%x", advice_flags);

    LogInfo("BEGIN INODE SCAN");

    struct inode_list *inode_list = get_inode_list(fs, target_path);

    /*
     * We now have a linked list of file paths to be scanned in
     * cb_data.list_start.
     */

    LogInfo("END INODE SCAN");

    inode_list = inode_list_sort(inode_list);

    LogInfo("BEGIN BLOCK SCAN");

    scan_blocks(fs, cb, inode_list);

    LogInfo("END BLOCK SCAN");

    int open_inodes_count = 0;
    uint64_t seeks = 0;
    uint64_t total_blocks = 0;

    struct block_list *block_list_start = NULL;
    struct block_list *block_list_end = NULL;

    while (inode_list != NULL)
    {
        /*
         * While there are inodes remaining and we're below the limit on open
         * inodes, add those inodes' blocks to the global list.
         */
        while (inode_list != NULL && open_inodes_count < max_inodes)
        {
            LogDebug("Adding blocks of inode %s (%llu bytes) to block read list", inode_list->path, inode_list->len);

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

        int max_inode_blocks = open_inodes_count > 0
            ? (max_blocks+open_inodes_count-1)/open_inodes_count : max_blocks;

        LogInfo("BEGIN BLOCK READ");

        while (block_list != NULL)
        {
            total_blocks++;

            struct stripe *stripe = next_stripe(fs->blocksize,
                                                coalesce_distance,
                                                max_inode_blocks, block_list);

            LogDebug("Found stripe of %lu blocks", stripe->consecutive_blocks);
            if (stripe->consecutive_blocks > 0)
            {
                read_stripe_data(fs->blocksize, block_list->physical_block,
                                 flags & ITERATE_OPT_DIRECT, fd, stripe);
            }
            else
                free(stripe);

            // stripe can be freed in heapify_stripe, so save consecutive_len here
            size_t consecutive_len = stripe->consecutive_len;

            block_list = heapify_stripe(fs, cb, block_list, stripe,
                                        max_inode_blocks, &open_inodes_count);

            // block is out of range
            if (consecutive_len == 0 && block_list != NULL)
            {
                struct block_list *old_next = block_list->next;
                *(prev_next_ptr) = block_list;
                block_list_end = block_list;
                prev_next_ptr = &block_list->next;
                block_list->next = NULL;

                block_list = old_next;
            }
        }

        LogInfo("END BLOCK READ");
    }

    double seeks_percentage = total_blocks == 0 ? 0. : ((double)seeks)/total_blocks * 100.;

    if (close(fd) != 0)
        exit_str("Error closing block device");

    if (ext2fs_close(fs) != 0)
        exit_str("Error closing file system");
}

void initialize_dj(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
    clog_init();
}
