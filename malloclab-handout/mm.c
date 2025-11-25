#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/* Alignment */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

/* Header/footer size */
#define WSIZE sizeof(size_t)
#define DSIZE (2 * WSIZE)
#define OVERHEAD (2 * WSIZE)

/* Extract size and alloc bit */
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~(size_t)(ALIGNMENT - 1))
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Header and footer from bp (payload pointer) */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - OVERHEAD)

/* Next and previous blocks in heap */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Global heap start */
static char *heap_start = NULL;

/* Forward declarations */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* ----------------------------------------
 * mm_init
 * ---------------------------------------- */
int mm_init(void)
{
    /* Allocate initial chunk to hold prologue/epilogue */
    size_t init_size = ALIGNMENT;
    heap_start = mem_map(init_size);
    if (heap_start == NULL) return -1;

    /* Prologue block: header + footer (allocated) */
    PUT(heap_start, PACK(OVERHEAD, 1));               // Prologue header
    PUT(heap_start + WSIZE, PACK(OVERHEAD, 1));       // Prologue footer

    /* Epilogue header at end */
    PUT(heap_start + DSIZE, PACK(0, 1));

    /* Extend the heap with an initial free block (~1 page) */
    if (extend_heap(mem_pagesize() / WSIZE) == NULL)
        return -1;

    return 0;
}

/* ----------------------------------------
 * extend_heap
 * ---------------------------------------- */
static void *extend_heap(size_t words)
{
    size_t size = ALIGN(words * WSIZE);
    if (size < OVERHEAD) size = OVERHEAD;

    void *new_block = mem_map(size);
    if (new_block == NULL) return NULL;

    /* Initialize free block: header + footer */
    PUT(new_block, PACK(size, 0));                // header
    PUT((char *)new_block + size - WSIZE, PACK(size, 0)); // footer

    /* New epilogue */
    PUT((char *)new_block + size, PACK(0, 1));

    /* Coalesce if previous was free */
    return coalesce((char *)new_block + WSIZE);
}

/* ----------------------------------------
 * coalesce
 * ---------------------------------------- */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* Case 1: both allocated */
    if (prev_alloc && next_alloc) {
        return bp;
    }
    /* Case 2: next free */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    /* Case 3: prev free */
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    /* Case 4: both free */
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    return bp;
}

/* ----------------------------------------
 * find_fit - first fit scan
 * ---------------------------------------- */
static void *find_fit(size_t asize)
{
    void *bp = heap_start + DSIZE; // skip prologue
    while (GET_SIZE(HDRP(bp)) > 0) {
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize)
            return bp;
        bp = NEXT_BLKP(bp);
    }
    return NULL;
}

/* ----------------------------------------
 * place - split block if large
 * ---------------------------------------- */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= (OVERHEAD + ALIGNMENT)) {
        /* Split */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        void *next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(csize - asize, 0));
        PUT(FTRP(next), PACK(csize - asize, 0));
    } else {
        /* Use whole block */
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* ----------------------------------------
 * mm_malloc
 * ---------------------------------------- */
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;

    size_t asize = ALIGN(size + OVERHEAD);
    if (asize < OVERHEAD + ALIGNMENT)
        asize = OVERHEAD + ALIGNMENT;

    void *bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit, extend heap */
    if ((bp = extend_heap(asize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/* ----------------------------------------
 * mm_free
 * ---------------------------------------- */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}
