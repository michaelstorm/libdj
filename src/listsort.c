#include <stdio.h>
#include <stdint.h>
#include "listsort.h"

/*
 * Demonstration code for sorting a linked list.
 * 
 * The algorithm used is Mergesort, because that works really well
 * on linked lists, without requiring the O(N) extra space it needs
 * when you do it on arrays.
 * 
 * This code can handle singly and doubly linked lists, and
 * circular and linear lists too. For any serious application,
 * you'll probably want to remove the conditionals on `is_circular'
 * and `is_double' to adapt the code to your own purpose. 
 * 
 */

/*
 * This file is copyright 2001 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This is the actual sort function. Notice that it returns the new
 * head of the list. (It has to, because the head will not
 * generally be the same element after the sort.) So unlike sorting
 * an array, where you can do
 * 
 *     sort(myarray);
 * 
 * you now have to do
 * 
 *     list = listsort(mylist);
 */
void *listsort(void *list, sort_cmp_func cmp, int next_offset) {
    char *p, *q, *e, *tail;
    int insize, nmerges, psize, qsize, i;

    if (!list)
	return NULL;

    insize = 1;

    while (1) {
        p = list;
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while (p) {
            nmerges++;  /* there exists a merge to be done */
            q = p;
            psize = 0;
            for (i = 0; i < insize; i++) {
                psize++;
                q = *(char **)(q+next_offset);
                if (!q) break;
            }

            qsize = insize;

            while (psize > 0 || (qsize > 0 && q)) {

                if (psize == 0) {
		    e = q; q = *(char **)(q+next_offset); qsize--;
		} else if (qsize == 0 || !q) {
		    e = p; p = *(char **)(p+next_offset); psize--;
		} else if (cmp(p, q) <= 0) {
		    e = p; p = *(char **)(p+next_offset); psize--;
		} else {
		    e = q; q = *(char **)(q+next_offset); qsize--;
		}

		if (tail) {
		    *(uintptr_t *)(tail+next_offset) = (uintptr_t)e;
		} else {
		    list = e;
		}
		tail = e;
            }

            p = q;
        }
	    *(uintptr_t *)(tail+next_offset) = (uintptr_t)NULL;
 
        if (nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        insize *= 2;
    }
}
