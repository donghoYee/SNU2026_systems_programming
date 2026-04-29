/*--------------------------------------------------------------------*/
/* chunk.c                                                            */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <assert.h>
#include "chunk.h"

/*--------------------------------------------------------------------*/
/* Internal struct definitions                                        */
/*--------------------------------------------------------------------*/

struct Chunk {
    int     status; /* CHUNK_FREE or CHUNK_USED */
    int     span;   /* total units: 1 header + payload + 1 footer */
    Chunk_T next;   /* next free block in the doubly-linked free list */
};

struct Footer {
    int     span;   /* mirrors header span for backward navigation */
    int     _pad;   /* padding for 8-byte alignment of prev pointer */
    Chunk_T prev;   /* previous free block in the doubly-linked free list */
};

typedef struct Footer *Footer_T;

/*--------------------------------------------------------------------*/
/* Internal helper: get the footer of block c                        */
/*--------------------------------------------------------------------*/

static Footer_T get_footer(Chunk_T c)
{
    return (Footer_T)((char *)c + (c->span - 1) * CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* Header accessors                                                   */
/*--------------------------------------------------------------------*/

int chunk_get_status(Chunk_T c)              { return c->status; }
void chunk_set_status(Chunk_T c, int status) { c->status = status; }

int chunk_get_span(Chunk_T c) { return c->span; }

void chunk_set_span(Chunk_T c, int span)
{
    c->span = span;
    get_footer(c)->span = span; /* keep footer in sync */
}

Chunk_T chunk_get_next_free(Chunk_T c)         { return c->next; }
void    chunk_set_next_free(Chunk_T c, Chunk_T n) { c->next = n; }

/*--------------------------------------------------------------------*/
/* Footer accessors                                                   */
/*--------------------------------------------------------------------*/

Chunk_T chunk_get_prev_free(Chunk_T c)            { return get_footer(c)->prev; }
void    chunk_set_prev_free(Chunk_T c, Chunk_T p) { get_footer(c)->prev = p; }

/*--------------------------------------------------------------------*/
/* Physical navigation                                                */
/*--------------------------------------------------------------------*/

Chunk_T chunk_get_next_phys(Chunk_T c, void *end)
{
    Chunk_T n = (Chunk_T)((char *)c + c->span * CHUNK_UNIT);
    return ((void *)n >= end) ? NULL : n;
}

Chunk_T chunk_get_prev_phys(Chunk_T c, void *start)
{
    Footer_T prev_foot;

    if ((void *)c <= start)
        return NULL;

    /* Read footer of the preceding block */
    prev_foot = (Footer_T)((char *)c - CHUNK_UNIT);
    return (Chunk_T)((char *)c - prev_foot->span * CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* Debug validity check                                               */
/*--------------------------------------------------------------------*/

#ifndef NDEBUG
int chunk_is_valid(Chunk_T c, void *start, void *end)
{
    Footer_T f;

    if (c == NULL) {
        fprintf(stderr, "chunk: null pointer\n");
        return 0;
    }
    if ((void *)c < start) {
        fprintf(stderr, "chunk: before heap start\n");
        return 0;
    }
    if ((void *)c >= end) {
        fprintf(stderr, "chunk: at or beyond heap end\n");
        return 0;
    }
    if (c->span < 2) {
        fprintf(stderr, "chunk: span %d < 2\n", c->span);
        return 0;
    }
    f = get_footer(c);
    if ((void *)f >= end) {
        fprintf(stderr, "chunk: footer beyond heap end\n");
        return 0;
    }
    if (f->span != c->span) {
        fprintf(stderr, "chunk: footer span %d != header span %d\n",
                f->span, c->span);
        return 0;
    }
    return 1;
}
#endif
