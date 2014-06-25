#include <stdlib.h>
#include <stdio.h>

#include "util.h"

char *prog_name;

void exit_str(char *message, ...)
{
    va_list args;
    va_start(args,  message);
    fprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void *emalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
        exit_str("Error allocating %d bytes of memory", size);
    return ptr;
}

void *ecalloc(size_t size)
{
    void *ptr = calloc(1, size);
    if (ptr == NULL)
        exit_str("Error allocating %d bytes of memory", size);
    return ptr;
}