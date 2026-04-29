/*--------------------------------------------------------------------*/
/* heapmgr1.c                                                         */
/* Dynamic memory manager using a doubly-linked free list with        */
/* boundary tags (header + footer per block).                         */
/*                                                                    */
/* Key design:                                                        */
/*   - Every block has a 1-unit header and a 1-unit footer.           */
/*   - Free list is doubly-linked, insertion at head (LIFO).          */
/*   - free() is O(1): backward navigation via footer lets us find    */
/*     and remove adjacent free blocks without list traversal.        */
/*   - malloc() is first-fit over the doubly-linked free list.        */
/*   - Coalescing on free (both left and right neighbors).            */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "chunk.h"
#include "../test/heapmgr.h"

#define FALSE 0
#define TRUE  1

/* Minimum payload units to request per sbrk call */
enum { SYS_MIN_ALLOC_UNITS = 512 };

static Chunk_T s_free_head = NULL;
static void   *s_heap_lo   = NULL;
static void   *s_heap_hi   = NULL;

/*--------------------------------------------------------------------*/
/* check_heap_validity                                                */
/*--------------------------------------------------------------------*/

#ifndef NDEBUG
static int check_heap_validity(void)
{
    Chunk_T w, prev_w, fl;

    if (s_heap_lo == NULL) {
        fprintf(stderr, "heap: uninitialized lo\n");
        return FALSE;
    }
    if (s_heap_hi == NULL) {
        fprintf(stderr, "heap: uninitialized hi\n");
        return FALSE;
    }
    if (s_heap_lo == s_heap_hi)
        return (s_free_head == NULL) ? TRUE : FALSE;

    /* Walk every physical block and check structural validity */
    prev_w = NULL;
    for (w = (Chunk_T)s_heap_lo; w != NULL;
         w = chunk_get_next_phys(w, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
            return FALSE;
        /* Coalescing invariant: no two adjacent free blocks */
        if (prev_w != NULL &&
            chunk_get_status(prev_w) == CHUNK_FREE &&
            chunk_get_status(w)      == CHUNK_FREE) {
            fprintf(stderr, "heap: uncoalesced adjacent free blocks\n");
            return FALSE;
        }
        prev_w = w;
    }

    /* Walk the doubly-linked free list and check consistency */
    for (fl = s_free_head; fl != NULL; fl = chunk_get_next_free(fl)) {
        Chunk_T next = chunk_get_next_free(fl);
        Chunk_T prev = chunk_get_prev_free(fl);

        if (chunk_get_status(fl) != CHUNK_FREE) {
            fprintf(stderr, "heap: non-free block in free list\n");
            return FALSE;
        }
        /* Head must have prev == NULL */
        if (fl == s_free_head && prev != NULL) {
            fprintf(stderr, "heap: free list head has non-null prev\n");
            return FALSE;
        }
        /* Check doubly-linked backward pointer of next node */
        if (next != NULL && chunk_get_prev_free(next) != fl) {
            fprintf(stderr, "heap: doubly-linked list inconsistency\n");
            return FALSE;
        }
    }

    return TRUE;
}
#endif /* NDEBUG */

/*--------------------------------------------------------------------*/
/* heap_bootstrap                                                     */
/*--------------------------------------------------------------------*/

static void heap_bootstrap(void)
{
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "heap: sbrk(0) failed\n");
        exit(1);
    }
}

/*--------------------------------------------------------------------*/
/* freelist_insert_head                                               */
/* Insert block c at the head of the doubly-linked free list. O(1).  */
/*--------------------------------------------------------------------*/

static void freelist_insert_head(Chunk_T c)
{
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_next_free(c, s_free_head);
    chunk_set_prev_free(c, NULL);

    if (s_free_head != NULL)
        chunk_set_prev_free(s_free_head, c);

    s_free_head = c;
}

/*--------------------------------------------------------------------*/
/* freelist_detach                                                    */
/* Remove block c from the doubly-linked free list. O(1).            */
/*--------------------------------------------------------------------*/

static void freelist_detach(Chunk_T c)
{
    Chunk_T next = chunk_get_next_free(c);
    Chunk_T prev = chunk_get_prev_free(c);

    if (prev != NULL)
        chunk_set_next_free(prev, next);
    else
        s_free_head = next;

    if (next != NULL)
        chunk_set_prev_free(next, prev);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    chunk_set_status(c, CHUNK_USED);
}

/*--------------------------------------------------------------------*/
/* do_alloc                                                           */
/* Given a free block cur with span >= need_span, either split it     */
/* (free at front, allocated at back) or use the whole block.        */
/* Returns the payload pointer of the allocated region.              */
/*--------------------------------------------------------------------*/

static void *do_alloc(Chunk_T cur, int need_span)
{
    int old_span    = chunk_get_span(cur);
    int remain_span = old_span - need_span;

    if (remain_span >= 2) {
        /*
         * Split: the leading portion stays free (cur, shrunk),
         * the trailing portion is the new allocated block.
         *
         * We must save cur's prev_free before changing the span because
         * chunk_set_span moves the footer to a new location, overwriting
         * whatever bytes were there (previously payload data).
         */
        Chunk_T saved_prev = chunk_get_prev_free(cur);
        Chunk_T alloc;

        chunk_set_span(cur, remain_span);       /* also syncs footer.span */
        chunk_set_prev_free(cur, saved_prev);   /* restore in new footer   */

        alloc = (Chunk_T)((char *)cur + (size_t)remain_span * CHUNK_UNIT);
        chunk_set_span(alloc, need_span);
        chunk_set_status(alloc, CHUNK_USED);
        chunk_set_next_free(alloc, NULL);
        chunk_set_prev_free(alloc, NULL);

        return (void *)((char *)alloc + CHUNK_UNIT);
    }

    /* Exact fit (or remain_span == 1 which can't form a valid block) */
    freelist_detach(cur);
    return (void *)((char *)cur + CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* sys_grow                                                           */
/* Extend the heap by at least need_units payload units.             */
/* Coalesces with the previous physical block if it is free.         */
/* Inserts the resulting block at the head of the free list.         */
/* Returns the new (possibly merged) free block, or NULL on failure. */
/*--------------------------------------------------------------------*/

static Chunk_T sys_grow(size_t need_units)
{
    size_t grow_data = (need_units < (size_t)SYS_MIN_ALLOC_UNITS)
                       ? (size_t)SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = grow_data + 2; /* header + payload + footer */
    Chunk_T c, prev;

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    if (c == (Chunk_T)-1)
        return NULL;

    s_heap_hi = sbrk(0);

    /* Initialise the new block */
    chunk_set_span(c, (int)grow_span); /* also writes footer.span */
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);

    /* Coalesce with the previous physical block if it is free */
    prev = chunk_get_prev_phys(c, s_heap_lo);
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        freelist_detach(prev);
        chunk_set_span(prev, chunk_get_span(prev) + (int)grow_span);
        c = prev;
    }

    freelist_insert_head(c);
    return c;
}

/*--------------------------------------------------------------------*/
/* heapmgr_malloc                                                     */
/*--------------------------------------------------------------------*/

void *heapmgr_malloc(size_t size)
{
    static int booted = FALSE;
    Chunk_T cur;
    size_t need_units;
    int need_span;

    if (size == 0)
        return NULL;

    if (!booted) {
        heap_bootstrap();
        booted = TRUE;
    }

    assert(check_heap_validity());

    need_units = (size + (size_t)(CHUNK_UNIT - 1)) / (size_t)CHUNK_UNIT;
    need_span  = (int)need_units + 2; /* header + payload + footer */

    /* First-fit search through the doubly-linked free list */
    for (cur = s_free_head; cur != NULL; cur = chunk_get_next_free(cur)) {
        if (chunk_get_span(cur) >= need_span) {
            void *p = do_alloc(cur, need_span);
            assert(check_heap_validity());
            return p;
        }
    }

    /* No suitable block found: grow the heap */
    cur = sys_grow(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    {
        void *p = do_alloc(cur, need_span);
        assert(check_heap_validity());
        return p;
    }
}

/*--------------------------------------------------------------------*/
/* heapmgr_free                                                       */
/*--------------------------------------------------------------------*/

void heapmgr_free(void *p)
{
    Chunk_T c, neighbor;

    if (p == NULL)
        return;

    assert(check_heap_validity());

    c = (Chunk_T)((char *)p - CHUNK_UNIT);
    assert(chunk_get_status(c) == CHUNK_USED);

    /*
     * Coalesce with right physical neighbor (O(1)):
     * We know the neighbor's address from c's span, and we can remove
     * it from the free list in O(1) using the doubly-linked pointers.
     */
    neighbor = chunk_get_next_phys(c, s_heap_hi);
    if (neighbor != NULL && chunk_get_status(neighbor) == CHUNK_FREE) {
        freelist_detach(neighbor);
        /* chunk_set_span syncs the new footer (at neighbor's old footer) */
        chunk_set_span(c, chunk_get_span(c) + chunk_get_span(neighbor));
    }

    /*
     * Coalesce with left physical neighbor (O(1)):
     * Read footer of the block immediately before c to find its header.
     */
    neighbor = chunk_get_prev_phys(c, s_heap_lo);
    if (neighbor != NULL && chunk_get_status(neighbor) == CHUNK_FREE) {
        freelist_detach(neighbor);
        chunk_set_span(neighbor, chunk_get_span(neighbor) + chunk_get_span(c));
        c = neighbor;
    }

    /* Insert the (possibly merged) block at the head of the free list */
    freelist_insert_head(c);

    assert(check_heap_validity());
}
