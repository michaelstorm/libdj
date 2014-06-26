#include "dj_util.h"
#include "dj_internal.h"

void print_inode_list(FILE *f, struct inode_list *list)
{
    while (list != NULL)
    {
        fprintf(f, "{index=%u, path=\"%s\", len=%lu, ", list->index, list->path,
                list->len);

        if (list->next == NULL)
            fprintf(f, "next=(nil), ");
        else
            fprintf(f, "next=%u, ", list->next->index);

        fprintf(f, "blocks_start=%p, ", list->blocks_start);
        fprintf(f, "blocks_end=%p}", list->blocks_end);

        if (list->next != NULL)
            fprintf(f, ", ");

        list = list->next;
    }
}

void print_block_list(FILE *f, struct block_list *list)
{
    while (list != NULL)
    {
        fprintf(f, "{inode_info=%u, physical_block=%llu, logical_block=%lld, "
                   "stripe_ptr={pos=%lu, len=%lu, stripe=%p}, next=%p}",
                list->inode_info->inode, list->physical_block,
                list->logical_block, list->stripe_ptr.pos, list->stripe_ptr.len,
                list->stripe_ptr.stripe, list->next);

        if (list->next != NULL)
            fprintf(f, ", ");

        list = list->next;
    }
}
