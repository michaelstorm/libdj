#include "block_scan.h"
#include "dj_internal.h"
#include "dj_util.h"

int nop_cb(uint32_t inode, char *path, uint64_t pos, uint64_t file_len,
           char *data, uint64_t data_len, void **private)
{
    return 0;
}

void test_scan_first_block()
{
    uint64_t physical_block = 17;
    uint64_t logical_block = 2;
    uint64_t file_len = 3498734;

    uint64_t block_size = 1024;

    struct inode_cb_info inode_info = {
        .len = file_len,
    };

    struct block_list block_list = {
        .inode_info = &inode_info,
        .physical_block = physical_block,
        .logical_block = logical_block,
        .next = NULL,
    };

    struct inode_list inode_list = {
        .len = file_len,
        .next = NULL,
        .blocks_start = NULL,
        .blocks_end = NULL,
    };

    struct scan_blocks_info scan_info = {
        .cb = nop_cb,
        .inode_info = &inode_info,
        .inode_list = &inode_list,
    };

    scan_block(block_size, physical_block, logical_block, &scan_info);

    fprintf(stdout, "inodes: ");
    print_inode_list(stdout, &inode_list);
    fprintf(stdout, "\nblocks: ");
    print_block_list(stdout, inode_list.blocks_start);
    fprintf(stdout, "\n");
}

int main(int argc, char **argv)
{
    test_scan_first_block();
    return 0;
}
