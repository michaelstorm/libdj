#include <stdio.h>
#include <string.h>

#include "ext-batching.h"
#include "md5.h"

int noop_block(uint32_t inode, char *path, uint64_t pos, uint64_t file_len, char *data, uint64_t data_len, void **private)
{
    return 0;
}

void usage(char *prog_name)
{
    fprintf(stderr, "Usage: %s [-md5|-crc] DEVICE DIRECTORY\n", prog_name);
    exit(1);
}

int main(int argc, char **argv)
{
    initialize_ext_batching(argv[0]);

    if (argc < 3 || argc > 4)
        usage(argv[0]);
    else if (argc == 3)
        iterate_dir(argv[1], argv[2], noop_block);
    else if (argc == 4)
    {
        if (!strcmp(argv[1], "-md5"))
            iterate_dir(argv[2], argv[3], file_md5);
        else if (!strcmp(argv[1], "-crc"))
        {
            fprintf(stderr, "CRC not yet implemented\n");
            exit(1);
        }
        else
            fprintf(stderr, "Unrecognized option %s\n", argv[1]);
    }
    return 0;
}
