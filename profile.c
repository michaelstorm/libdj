#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "dj.h"

struct timespec tp_diff(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec-start.tv_nsec)<0) {
        temp.tv_sec = end.tv_sec-start.tv_sec-1;
        temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec-start.tv_sec;
        temp.tv_nsec = end.tv_nsec-start.tv_nsec;
    }
    return temp;
}

struct timespec profile_seek_reads(int fd, int op_count, size_t read_size, size_t seek_size, int seek_ratio_numer, int seek_ratio_denom)
{
    size_t physical_read_len = ((read_size+511)/512)*512;
    char *data;
    if (posix_memalign((void **)&data, 512, physical_read_len))
        perror("Error allocating aligned memory");

    if (lseek(fd, 0, SEEK_SET) != 0)
        perror("Could not seek to beginning of disk");

    struct timespec start_tp, end_tp, diff_tp;
    if (clock_gettime(CLOCK_MONOTONIC, &start_tp))
        perror("Could not get time");

    int remaining_op_count = op_count;
    off_t physical_pos = 0;
    for (int i = 0; i < remaining_op_count; i++)
    {
        if (seek_ratio_denom == 1 || (seek_ratio_denom != 0 && ((i % seek_ratio_denom) < seek_ratio_numer)))
        {
            physical_pos += seek_size;
            if (lseek(fd, physical_pos, SEEK_SET) != physical_pos)
                perror("Could not seek to beginning of disk");
            //remaining_op_count--;
        }
        else
        {
            /*if (pread(fd, data, physical_read_len, physical_pos) < read_size)
                perror("Error reading from block device");*/
            if (read(fd, data, physical_read_len) < read_size)
                perror("Error reading from block device");
            physical_pos += read_size;
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end_tp))
        perror("Could not get time");

    return tp_diff(start_tp, end_tp);
}

int main(int argc, char **argv)
{
    char *dev_path = argv[1];

    int fd;
    if ((fd = open(dev_path, O_RDONLY | O_DIRECT)) < 0)
    {
        perror("open");
        exit(1);
    }

    size_t read_sizes[] = { 4096, 8192 };
    size_t seek_sizes[] = { 4096, 8192, 32768 };
    int read_size_count = sizeof(read_sizes) / sizeof(size_t);
    int seek_size_count = sizeof(seek_sizes) / sizeof(size_t);

    printf("read_size seek_size seek_ratio time\n");

    for (int r = 0; r < read_size_count; r++)
    {
        for (int s = 0; s < seek_size_count; s++)
        {
            size_t read_size = read_sizes[r];
            size_t seek_size = seek_sizes[s];

            struct timespec op_tp;
            for (int i = 0; i <= 10; i++)
            {
                double ratio = ((double)i)/((double)10);
                op_tp = profile_seek_reads(fd, 160000, read_size, seek_size, i, 10);
                printf("%zu %zu %lf %ld.%ld\n", read_size, seek_size, ratio, op_tp.tv_sec, op_tp.tv_nsec);
            }
        }
    }

    return 0;
}
