#include "block_scan.h"
#include "clog.h"
#include "util.h"

struct scan_blocks_info
{
    block_cb cb;
    struct inode_cb_info *inode_info;
    struct inode_list *inode_list;
};

/*
 * Callback (indirectly) invoked by libext2fs for each block of a file.
 * - Increments the reference count of the block's inode.
 * - Sets the block's metadata (such as logical and physical numbers and length)
 *   in a block_list struct.
 * - Inserts the block_list struct into the inode's linked list of its blocks.
 * - Recursively calls itself to add blocks for holes.
 */
void scan_block(uint64_t block_size, blk64_t physical_block,
                e2_blkcnt_t logical_block, struct scan_blocks_info *scan_info)
{
    // Ignore the extra "empty" block at the end of a file, to allow appending
    // for writers, when we pass in BLOCK_FLAG_HOLE, unless the file is empty
    if (logical_block * block_size >= scan_info->inode_info->len
        || scan_info->inode_info->len == 0)
    {
        return;
    }

    struct inode_list *inode_list = scan_info->inode_list;
    struct block_list *blocks_end = inode_list->blocks_end;
    struct block_list *list;

    if (blocks_end != NULL && (blocks_end->physical_block + blocks_end->num_blocks) == physical_block)
    {
        list = blocks_end;
    }
    else
    {
        list = ecalloc(sizeof(struct block_list));
        list->inode_info = scan_info->inode_info;
        list->inode_info->references++;
        list->physical_block = physical_block;
        list->logical_block = logical_block;

        // FIXME what happens when holes are at the end of the file?
        for (e2_blkcnt_t i = list->inode_info->blocks_scanned;
             i < logical_block; i++)
        {
            // sparse files' hole blocks should be passed to this function, since we
            // passed BLOCK_FLAG_HOLE to the iterator function, but that doesn't
            // seem to be happening - so fix it
            scan_block(block_size, 0, i, scan_info);
        }

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
    }

    scan_info->inode_info->blocks_scanned++;
    list->num_blocks++;

    uint64_t logical_pos = list->logical_block * block_size;
    uint64_t remaining_len = list->inode_info->len - logical_pos;
    uint64_t simple_len = list->num_blocks * block_size;
    list->stripe_ptr.len = simple_len > remaining_len ? remaining_len : simple_len;

    LogTrace("Physical block %lu (%lu) is logical block %lu (%lu) of size %lu for inode %d", list->physical_block + list->num_blocks - 1, list->physical_block, list->logical_block + list->num_blocks - 1, list->logical_block, list->stripe_ptr.len, scan_info->inode_info->inode);
}

/*
 * Wraps the actual callback with a function whose signature libext2fs expects,
 * which makes the actual function easier to test.
 */
int scan_block_cb(ext2_filsys fs, blk64_t *blocknr, e2_blkcnt_t blockcnt,
                  blk64_t ref_blk, int ref_offset, void *private)
{
    scan_block(fs->blocksize, *blocknr, blockcnt, private);
    return 0;
}

void scan_blocks(ext2_filsys fs, block_cb cb, struct inode_list *inode_list)
{
    char block_buf[fs->blocksize * 3];
    struct scan_blocks_info scan_info = { cb, NULL, NULL };

    /*
     * For each inode, add the metadata for each of that inode's blocks to the
     * inode's block list.
     */
    struct inode_list *scan_inode_list = inode_list;
    while (scan_inode_list != NULL)
    {
        struct inode_cb_info *info = ecalloc(sizeof(struct inode_cb_info));
        info->inode = scan_inode_list->index;
        info->path = emalloc(strlen(scan_inode_list->path)+1);
        strcpy(info->path, scan_inode_list->path);
        info->len = scan_inode_list->len;

        LogDebug("Scanning blocks of inode %d: %s", info->inode, info->path);

        // there's some duplication of information (path and len) between
        // scan_info.inode_info and .inode_list, but that's ok
        scan_info.inode_info = info;
        scan_info.inode_list = scan_inode_list;

        int iter_flags = BLOCK_FLAG_HOLE | BLOCK_FLAG_DATA_ONLY
                         | BLOCK_FLAG_READ_ONLY;
        CHECK_FATAL(ext2fs_block_iterate3(fs, info->inode, iter_flags,
                                          block_buf, scan_block_cb, &scan_info),
                "while iterating over blocks of inode %d", info->inode);

        if (info->references == 0)
        {
            // empty files generate no blocks, so we'd get into an infinite loop
            // below
            scan_info.cb(info->inode, info->path, 0, 0, NULL, 0,
                         &info->cb_private);
            free(info->path);
            free(info);
        }

        scan_inode_list = scan_inode_list->next;
    }
}