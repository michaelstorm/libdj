struct inode_list
{
    ext2_ino_t index;
    char *path;
    uint64_t len;
    struct inode_list *next;

    struct block_list *blocks_start;
    struct block_list *blocks_end;
};

struct stripe
{
    char *data;
    e2_blkcnt_t references;

    // total number of consecutive blocks, excluding gaps
    e2_blkcnt_t consecutive_blocks;

    // total length of consecutive blocks in bytes, including gaps
    size_t consecutive_len;
};

struct stripe_pointer
{
    struct stripe *stripe;
    off_t pos;
    size_t len;
};

struct block_list
{
    struct inode_cb_info *inode_info;
    blk64_t physical_block;
    e2_blkcnt_t logical_block;
    struct stripe_pointer stripe_ptr;
    struct block_list *next;
};
