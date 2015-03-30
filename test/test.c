#include "dj_internal.h"
#include "dj_util.h"

int nop_cb(uint32_t inode, char *path, uint64_t pos, uint64_t file_len,
           char *data, uint64_t data_len, void **private)
{
    return 0;
}

#define TEST_SCAN_BLOCK(_physical_block, _logical_block, _file_len) \
    uint64_t block_size = 1024; \
    \
    struct inode_cb_info inode_info = { \
        .len = _file_len, \
    }; \
    \
    struct block_list block_list = { \
        .inode_info = &inode_info, \
        .physical_block = _physical_block, \
        .logical_block = _logical_block, \
        .next = NULL, \
    }; \
    \
    struct inode_list inode_list = { \
        .len = _file_len, \
        .next = NULL, \
        .blocks_start = NULL, \
        .blocks_end = NULL, \
    }; \
    \
    struct scan_blocks_info scan_info = { \
        .cb = nop_cb, \
        .inode_info = &inode_info, \
        .inode_list = &inode_list, \
    }; \
    \
    scan_block(block_size, _physical_block, _logical_block, &scan_info); \
    \
    fprintf(stdout, "inodes: "); \
    print_inode_list(stdout, &inode_list); \
    fprintf(stdout, "\nblocks: "); \
    print_block_list(stdout, inode_list.blocks_start); \
    fprintf(stdout, "\n");

void test_scan_first_block()
{
    TEST_SCAN_BLOCK(17, 2, 3498734)
}

int main(int argc, char **argv)
{
    test_scan_first_block();
    return 1;
}
