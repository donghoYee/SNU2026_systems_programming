/*--------------------------------------------------------------------*/
/* chunk.h                                                            */
/* Chunk (block) abstraction for doubly-linked free list with        */
/* boundary tags (header + footer per block).                        */
/*--------------------------------------------------------------------*/

#ifndef _CHUNK_H_
#define _CHUNK_H_

#include <stddef.h>
#include <unistd.h>

typedef struct Chunk *Chunk_T;

enum {
    CHUNK_FREE = 0,
    CHUNK_USED = 1
};

/*
 * CHUNK_UNIT: size of one unit in bytes.
 * Assumes 64-bit pointers: struct Chunk = int(4)+int(4)+ptr(8) = 16 bytes.
 * struct Footer = int(4)+int(4)+ptr(8) = 16 bytes.
 * Both header and footer occupy exactly one unit.
 */
enum {
    CHUNK_UNIT = 16
};

/*--------------------------------------------------------------------*/
/* Header field accessors                                             */
/*--------------------------------------------------------------------*/

int     chunk_get_status(Chunk_T c);
void    chunk_set_status(Chunk_T c, int status);

/* chunk_set_span also syncs the footer's span field. */
int     chunk_get_span(Chunk_T c);
void    chunk_set_span(Chunk_T c, int span);

Chunk_T chunk_get_next_free(Chunk_T c);
void    chunk_set_next_free(Chunk_T c, Chunk_T next);

/*--------------------------------------------------------------------*/
/* Footer field accessors                                             */
/* (footer is the last CHUNK_UNIT of the block)                      */
/*--------------------------------------------------------------------*/

/* prev_free pointer stored in the block's footer */
Chunk_T chunk_get_prev_free(Chunk_T c);
void    chunk_set_prev_free(Chunk_T c, Chunk_T prev);

/*--------------------------------------------------------------------*/
/* Physical navigation                                                */
/*--------------------------------------------------------------------*/

/* Returns next adjacent block, or NULL if at/past end. */
Chunk_T chunk_get_next_phys(Chunk_T c, void *end);

/* Returns previous adjacent block by reading its footer,
   or NULL if c is at the start of the heap. */
Chunk_T chunk_get_prev_phys(Chunk_T c, void *start);

/*--------------------------------------------------------------------*/
/* Debug validity check                                               */
/*--------------------------------------------------------------------*/

#ifndef NDEBUG
/* Returns 1 if chunk is valid (bounds, span >= 2, footer matches). */
int chunk_is_valid(Chunk_T c, void *start, void *end);
#endif

#endif /* _CHUNK_H_ */
