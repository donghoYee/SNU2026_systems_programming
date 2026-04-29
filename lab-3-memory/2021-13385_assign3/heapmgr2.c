/*--------------------------------------------------------------------*/
/* heapmgr2.c                                                         */
/* Dynamic memory manager using segregated free lists with            */
/* boundary tags (header + footer per block).                         */
/*                                                                    */
/* Key design:                                                        */
/*   - Every block has a 1-unit header and a 1-unit footer.           */
/*   - EXACT_BINS bins for small spans (exact size classes).          */
/*   - LOG_BINS bins for large spans (logarithmic size classes).      */
/*   - malloc() is O(1): jump to the right bin, scan at most          */
/*     (NUM_BINS - first_bin) bins to find a fit.                     */
/*   - free() is O(1): coalesce both neighbors, insert at bin head.   */
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

/*
 * Bin layout:
 *   bins[0]        → span == 2  (payload = 0 units, 0 bytes)
 *   bins[1]        → span == 3  (payload = 1 unit,  16 bytes)
 *   ...
 *   bins[EXACT_BINS-1] → span == EXACT_BINS+1
 *   bins[EXACT_BINS]   → spans in [EXACT_BINS+2 .. 2*(EXACT_BINS+1))
 *   bins[EXACT_BINS+1] → spans in [2*(EXACT_BINS+1) .. 4*(EXACT_BINS+1))
 *   ...
 */
enum {
    EXACT_BINS = 64,   /* exact-fit classes for spans 2..65 */
    LOG_BINS   = 32,   /* logarithmic classes for larger spans */
    NUM_BINS   = EXACT_BINS + LOG_BINS
};

static Chunk_T s_bins[NUM_BINS];
static void   *s_heap_lo = NULL;
static void   *s_heap_hi = NULL;

/*--------------------------------------------------------------------*/
/* get_bin_index                                                      */
/* Maps a span to a bin index.  Bin 0 handles span==2, etc.          */
/*--------------------------------------------------------------------*/

static int get_bin_index(int span)
{
    if (span < 2 + EXACT_BINS)
        return span - 2;                      /* 0 .. EXACT_BINS-1 */

    /* Logarithmic bins: find floor(log2(span / (EXACT_BINS+1))) */
    int k = 0;
    int s = span >> 6;  /* divide by 64 (≈ EXACT_BINS+1 rounded) */
    while (s > 1 && EXACT_BINS + k < NUM_BINS - 1) {
        s >>= 1;
        k++;
    }
    return EXACT_BINS + k;
}

/*--------------------------------------------------------------------*/
/* get_search_bin                                                     */
/* Returns the lowest bin index whose blocks are all >= need_span.   */
/* For exact bins we may need to start one bin higher if the exact   */
/* bin size is smaller than need_span (covered by get_bin_index).    */
/*--------------------------------------------------------------------*/

static int get_search_bin(int need_span)
{
    return get_bin_index(need_span);
}

/*--------------------------------------------------------------------*/
/* check_heap_validity                                                */
/*--------------------------------------------------------------------*/

#ifndef NDEBUG
static int check_heap_validity(void)
{
    int b;
    Chunk_T w, prev_w, fl;

    if (s_heap_lo == NULL) {
        fprintf(stderr, "heap: uninitialized lo\n");
        return FALSE;
    }
    if (s_heap_hi == NULL) {
        fprintf(stderr, "heap: uninitialized hi\n");
        return FALSE;
    }

    if (s_heap_lo == s_heap_hi) {
        for (b = 0; b < NUM_BINS; b++) {
            if (s_bins[b] != NULL) {
                fprintf(stderr, "heap: empty heap but non-null bin %d\n", b);
                return FALSE;
            }
        }
        return TRUE;
    }

    /* Walk every physical block */
    prev_w = NULL;
    for (w = (Chunk_T)s_heap_lo; w != NULL;
         w = chunk_get_next_phys(w, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi))
            return FALSE;
        if (prev_w != NULL &&
            chunk_get_status(prev_w) == CHUNK_FREE &&
            chunk_get_status(w)      == CHUNK_FREE) {
            fprintf(stderr, "heap: uncoalesced adjacent free blocks\n");
            return FALSE;
        }
        prev_w = w;
    }

    /* Walk every bin's free list */
    for (b = 0; b < NUM_BINS; b++) {
        for (fl = s_bins[b]; fl != NULL; fl = chunk_get_next_free(fl)) {
            Chunk_T next = chunk_get_next_free(fl);
            Chunk_T prev = chunk_get_prev_free(fl);

            if (chunk_get_status(fl) != CHUNK_FREE) {
                fprintf(stderr, "heap: non-free block in bin %d\n", b);
                return FALSE;
            }
            if (fl == s_bins[b] && prev != NULL) {
                fprintf(stderr, "heap: bin %d head has non-null prev\n", b);
                return FALSE;
            }
            if (next != NULL && chunk_get_prev_free(next) != fl) {
                fprintf(stderr, "heap: doubly-linked inconsistency in bin %d\n", b);
                return FALSE;
            }
            if (get_bin_index(chunk_get_span(fl)) != b) {
                fprintf(stderr, "heap: block span %d in wrong bin %d\n",
                        chunk_get_span(fl), b);
                return FALSE;
            }
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
    int i;
    for (i = 0; i < NUM_BINS; i++)
        s_bins[i] = NULL;
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "heap: sbrk(0) failed\n");
        exit(1);
    }
}

/*--------------------------------------------------------------------*/
/* freelist_insert                                                    */
/* Insert block c at the head of the appropriate bin. O(1).          */
/*--------------------------------------------------------------------*/

static void freelist_insert(Chunk_T c)
{
    int idx = get_bin_index(chunk_get_span(c));
    Chunk_T head = s_bins[idx];

    chunk_set_status(c, CHUNK_FREE);
    chunk_set_next_free(c, head);
    chunk_set_prev_free(c, NULL);

    if (head != NULL)
        chunk_set_prev_free(head, c);

    s_bins[idx] = c;
}

/*--------------------------------------------------------------------*/
/* freelist_detach                                                    */
/* Remove block c from its bin. O(1).                                */
/*--------------------------------------------------------------------*/

static void freelist_detach(Chunk_T c)
{
    Chunk_T next = chunk_get_next_free(c);
    Chunk_T prev = chunk_get_prev_free(c);

    if (prev != NULL)
        chunk_set_next_free(prev, next);
    else {
        int idx = get_bin_index(chunk_get_span(c));
        s_bins[idx] = next;
    }

    if (next != NULL)
        chunk_set_prev_free(next, prev);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    chunk_set_status(c, CHUNK_USED);
}

/*--------------------------------------------------------------------*/
/* do_alloc                                                           */
/* Given a free block cur with span >= need_span, split or use it.   */
/* Returns the payload pointer of the allocated region.              */
/*--------------------------------------------------------------------*/

static void *do_alloc(Chunk_T cur, int need_span)
{
    int old_span    = chunk_get_span(cur);
    int remain_span = old_span - need_span;

    freelist_detach(cur);  /* remove cur from its bin first */

    if (remain_span >= 2) {
        /*
         * Split: leading portion becomes a new free block,
         * trailing portion is the allocated block.
         * (Opposite layout from heapmgr1 to avoid the saved_prev issue.)
         */
        Chunk_T alloc = cur;
        Chunk_T rem;

        chunk_set_span(alloc, need_span);
        chunk_set_status(alloc, CHUNK_USED);
        chunk_set_next_free(alloc, NULL);
        chunk_set_prev_free(alloc, NULL);

        rem = (Chunk_T)((char *)alloc + (size_t)need_span * CHUNK_UNIT);
        chunk_set_span(rem, remain_span);
        chunk_set_next_free(rem, NULL);
        chunk_set_prev_free(rem, NULL);
        freelist_insert(rem);

        return (void *)((char *)alloc + CHUNK_UNIT);
    }

    /* Exact fit (or remain_span == 1 which can't form a valid block) */
    return (void *)((char *)cur + CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* sys_grow                                                           */
/* Extend the heap by at least need_units payload units.             */
/* Coalesces with the previous physical block if it is free.         */
/* Inserts the resulting block into the appropriate bin.             */
/* Returns the new (possibly merged) free block, or NULL on failure. */
/*--------------------------------------------------------------------*/

static Chunk_T sys_grow(size_t need_units)
{
    size_t grow_data = (need_units < (size_t)SYS_MIN_ALLOC_UNITS)
                       ? (size_t)SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = grow_data + 2;
    Chunk_T c, prev;

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    if (c == (Chunk_T)-1)
        return NULL;

    s_heap_hi = sbrk(0);

    chunk_set_span(c, (int)grow_span);
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);

    prev = chunk_get_prev_phys(c, s_heap_lo);
    if (prev != NULL && chunk_get_status(prev) == CHUNK_FREE) {
        freelist_detach(prev);
        chunk_set_span(prev, chunk_get_span(prev) + (int)grow_span);
        c = prev;
    }

    freelist_insert(c);
    return c;
}

/*--------------------------------------------------------------------*/
/* heapmgr_malloc                                                     */
/*--------------------------------------------------------------------*/

void *heapmgr_malloc(size_t size)
{
    static int booted = FALSE;
    size_t need_units;
    int need_span, b;

    if (size == 0)
        return NULL;

    if (!booted) {
        heap_bootstrap();
        booted = TRUE;
    }

    assert(check_heap_validity());

    need_units = (size + (size_t)(CHUNK_UNIT - 1)) / (size_t)CHUNK_UNIT;
    need_span  = (int)need_units + 2;

    /* Search bins from the smallest bin that could satisfy need_span */
    for (b = get_search_bin(need_span); b < NUM_BINS; b++) {
        Chunk_T cur;
        for (cur = s_bins[b]; cur != NULL; cur = chunk_get_next_free(cur)) {
            if (chunk_get_span(cur) >= need_span) {
                void *p = do_alloc(cur, need_span);
                assert(check_heap_validity());
                return p;
            }
        }
    }

    /* No suitable block found: grow the heap */
    {
        Chunk_T cur = sys_grow(need_units);
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

    /* Coalesce with right physical neighbor */
    neighbor = chunk_get_next_phys(c, s_heap_hi);
    if (neighbor != NULL && chunk_get_status(neighbor) == CHUNK_FREE) {
        freelist_detach(neighbor);
        chunk_set_span(c, chunk_get_span(c) + chunk_get_span(neighbor));
    }

    /* Coalesce with left physical neighbor */
    neighbor = chunk_get_prev_phys(c, s_heap_lo);
    if (neighbor != NULL && chunk_get_status(neighbor) == CHUNK_FREE) {
        freelist_detach(neighbor);
        chunk_set_span(neighbor, chunk_get_span(neighbor) + chunk_get_span(c));
        c = neighbor;
    }

    freelist_insert(c);

    assert(check_heap_validity());
}
