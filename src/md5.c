#include <stdio.h>
#include <stdlib.h>
#include <openssl/md5.h>

#include "dj.h"

int file_md5(uint32_t inode, char *path, uint64_t pos, uint64_t file_len,
             char *data, uint64_t data_len, void **private)
{
    MD5_CTX *ctx;
    if (pos == 0)
    {
        ctx = emalloc(sizeof(MD5_CTX));
        if (ctx == NULL)
        {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }

        MD5_Init(ctx);
        *(MD5_CTX **)private = ctx;
    }
    else
        ctx = *(MD5_CTX **)private;

    MD5_Update(ctx, data, data_len);

    if (pos + data_len == file_len)
    {
        unsigned char md_buf[MD5_DIGEST_LENGTH];
        MD5_Final(md_buf, ctx);
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            printf("%02x", md_buf[i]);
        printf("  %s\n", path);
        free(ctx);
    }
    return 0;
}
