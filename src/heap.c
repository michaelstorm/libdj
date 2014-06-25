#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"

struct heap *heap_create(int size)
{
    struct heap *heap = malloc(sizeof(struct heap));
    if (heap == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for heap array");
        exit(1);
    }

    heap->max_size = size;
    heap->current_size = 0;
    heap->elems = malloc(sizeof(struct heap_elem) * size);
    return heap;
}

void heap_destroy(struct heap *heap)
{
    free(heap->elems);
    free(heap);
}

void print_heap(FILE *f, struct heap *heap)
{
    fprintf(f, "heap size %d: ", heap->current_size);
    for (int i = 0; i < heap->current_size; i++)
    {
        fprintf(f, "%lu%s", heap->elems[i].key,
                i < heap->current_size-1 ? ", " : "");
    }
    fprintf(f, "\n");
}

int heap_size(struct heap *heap)
{
    return heap->current_size;
}

void swap_heap_elems(struct heap_elem *first, struct heap_elem *second)
{
    struct heap_elem tmp_elem = *first;
    *first = *second;
    *second = tmp_elem;
}

void heapify_up(struct heap *heap, int index)
{
    while (index > 0)
    {
        int parent_index = (index-1)/2;

        struct heap_elem *current_elem = &heap->elems[index];
        struct heap_elem *parent_elem = &heap->elems[parent_index];
        if (current_elem->key < parent_elem->key)
        {
            swap_heap_elems(current_elem, parent_elem);

            //fprintf(stderr, "swapped index %d with %d\n", index, parent_index);

            index = parent_index;
        }
        else
            break;
    }
}

void heapify_down(struct heap *heap, int index)
{
    while (index < heap->current_size)
    {
        struct heap_elem *current_elem = &heap->elems[index];

        int first_child_index = 2 * index + 1;
        int second_child_index = 2 * (index + 1);

        struct heap_elem *first_child_elem =
            first_child_index < heap->current_size
                ? &heap->elems[first_child_index]
                : NULL;

        struct heap_elem *second_child_elem =
            second_child_index < heap->current_size
                ? &heap->elems[second_child_index]
                : NULL;

        int first_smaller = first_child_elem != NULL
                            && first_child_elem->key < current_elem->key;

        int second_smaller = second_child_elem != NULL
                             && second_child_elem->key < current_elem->key;

        int swap_index = 0;
        if (first_smaller && second_smaller)
        {
            swap_index =
                first_child_elem->key < second_child_elem->key
                    ? first_child_index
                    : second_child_index;
        }
        else if (first_smaller)
            swap_index = first_child_index;
        else if (second_smaller)
            swap_index = second_child_index;

        if (swap_index > 0)
        {
            swap_heap_elems(current_elem, &heap->elems[swap_index]);
            index = swap_index;
        }
        else
            break;
    }
}

void heap_insert(struct heap *heap, uint64_t key, void *value)
{
    if (heap->current_size == heap->max_size)
        exit(1);

    int index = heap->current_size++;
    struct heap_elem *elem = &heap->elems[index];
    elem->key = key;
    elem->value = value;

    heapify_up(heap, index);
}

void *heap_min(struct heap *heap)
{
    return heap->elems[0].value;
}

void *heap_delmin(struct heap *heap)
{
    void *old_min = heap->elems[0].value;

    int last_index = --heap->current_size;
    heap->elems[0] = heap->elems[last_index];

    heapify_down(heap, 0);

    return old_min;
}

void verify_heap(struct heap *heap)
{
    for (int i = 0; i < heap->current_size; i++)
    {
        struct heap_elem *current_elem = &heap->elems[i];

        int first_child_index = 2 * i + 1;
        int second_child_index = 2 * (i + 1);

        struct heap_elem *first_child_elem =
            first_child_index < heap->current_size
                ? &heap->elems[first_child_index]
                : NULL;

        struct heap_elem *second_child_elem =
            second_child_index < heap->current_size
                ? &heap->elems[second_child_index]
                : NULL;

        int first_greater = first_child_elem == NULL
                            || first_child_elem->key > current_elem->key;

        int second_greater = second_child_elem == NULL
                            || second_child_elem->key > current_elem->key;

        if (!first_greater)
        {
            fprintf(stderr, "heap property violated at index %d (key %lu) "
                            "because its first child's key is %lu\n",
                    i, heap->elems[i].key, first_child_elem->key);
        }
        if (!second_greater)
        {
            fprintf(stderr, "heap property violated at index %d (key %lu) "
                            "because its second child's key is %lu\n",
                    i, heap->elems[i].key, second_child_elem->key);
        }

        if (!first_greater || !second_greater)
            exit(1);
    }
}
