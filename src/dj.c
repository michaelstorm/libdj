#include <fcntl.h>
#include <unistd.h>

#include "block_scan.h"
#include "clog.h"
#include "dir_scan.h"
#include "dj_internal.h"
#include "listsort.h"
#include "stripe.h"
#include "util.h"

// inode indexes are unsigned ints, so don't subtract them; semicolons seem to
// help Sublime Text's syntax parser
SORT_FUNC(inode_list_sort, struct inode_list, (p->index < q->index ? -1 : 1));
SORT_FUNC(block_list_sort, struct block_list,
          (p->physical_block < q->physical_block ? -1 : 1));

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

void dj_init(char *error_prog_name)
{
    prog_name = error_prog_name;
    initialize_ext2_error_table();
    clog_init();
}

void dj_free()
{
    clog_free();
}