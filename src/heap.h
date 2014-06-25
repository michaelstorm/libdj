#ifndef HEAP_H
#define HEAP_H

#include <stdio.h>

struct heap_elem
{
    uint64_t key;
    void *value;
};
struct heap
{
    int max_size;
    int current_size;
    struct heap_elem *elems;
};

struct heap *heap_create(int size);
void heap_destroy(struct heap *heap);
void print_heap(FILE *f, struct heap *heap);
int heap_size(struct heap *heap);
void heap_insert(struct heap *heap, uint64_t key, void *value);
void *heap_min(struct heap *heap);
void *heap_delmin(struct heap *heap);
void verify_heap(struct heap *heap);

#endif
