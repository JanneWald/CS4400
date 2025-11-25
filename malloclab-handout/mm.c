#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* Basic constants and macros */
#define WSIZE 4        /* Word size (bytes) */
#define DSIZE 8        /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)  /* Initial heap size (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header */
#define HDRP(bp) ((char *)(bp) - WSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* Free list pointers - for free blocks only */
#define GET_NEXT(bp) (*(void **)(bp))
#define GET_PREV(bp) (*(void **)((char *)(bp) + WSIZE))
#define SET_NEXT(bp, val) (*(void **)(bp) = (val))
#define SET_PREV(bp, val) (*(void **)((char *)(bp) + WSIZE) = (val))

/* Minimum block size - header + next/prev pointers + alignment */
#define MIN_BLOCK_SIZE (ALIGN(WSIZE + DSIZE))

static void *free_list_head = NULL;

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Reset free list */
    free_list_head = NULL;
    
    /* Start with an initial heap chunk */
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    
    return 0;
}

/* 
 * mm_malloc - Allocate a block from the free list.
 */
void *mm_malloc(size_t size)
{
    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    void *bp;
    
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;
    
    /* Adjust block size to include overhead and alignment reqs */
    asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;
    
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize)) == NULL)
        return NULL;
    
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block and coalesce with neighbors.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;
    
    size_t size = GET_SIZE(HDRP(ptr));
    
    /* Mark as free */
    PUT(HDRP(ptr), PACK(size, 0));
    
    /* Add block to free list */
    insert_free_block(ptr);
    
    /* Coalesce if the neighbors are free */
    coalesce(ptr);
}

/*
 * extend_heap - Extend heap with a new free block.
 */
static void *extend_heap(size_t size)
{
    char *bp;
    size_t asize = ALIGN(size);
    
    if ((bp = mem_map(asize)) == NULL)
        return NULL;
    
    /* Initialize free block header */
    PUT(HDRP(bp), PACK(asize, 0));
    
    /* Initialize free block pointers */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    
    /* Add to free list */
    insert_free_block(bp);
    
    return bp;
}

/*
 * coalesce - Boundary tag coalescing.
 */
static void *coalesce(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void *prev = PREV_BLKP(bp);
    void *next = NEXT_BLKP(bp);
    
    /* Check if previous block exists and is free */
    int prev_free = (prev < bp) && !GET_ALLOC(HDRP(prev));
    
    /* Check if next block exists and is free */
    int next_free = !GET_ALLOC(HDRP(next));
    
    if (prev_free && next_free) {
        /* Case 4: coalesce both sides */
        remove_free_block(prev);
        remove_free_block(next);
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(next));
        PUT(HDRP(prev), PACK(size, 0));
        bp = prev;
    }
    else if (prev_free) {
        /* Case 3: coalesce with previous block */
        remove_free_block(prev);
        size += GET_SIZE(HDRP(prev));
        PUT(HDRP(prev), PACK(size, 0));
        bp = prev;
    }
    else if (next_free) {
        /* Case 2: coalesce with next block */
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
    }
    /* Case 1: no coalescing needed */
    
    /* Update free list for coalesced block */
    if (prev_free || next_free) {
        SET_NEXT(bp, NULL);
        SET_PREV(bp, NULL);
        insert_free_block(bp);
    }
    
    return bp;
}

/*
 * find_fit - First-fit search of the free list.
 */
static void *find_fit(size_t asize)
{
    void *bp = free_list_head;
    
    while (bp != NULL) {
        if (GET_SIZE(HDRP(bp)) >= asize)
            return bp;
        bp = GET_NEXT(bp);
    }
    return NULL; /* No fit */
}

/*
 * place - Place the allocated block and split if remainder is large enough.
 */
static void place(void *bp, size_t asize)
{
    size_t total_size = GET_SIZE(HDRP(bp));
    
    remove_free_block(bp);
    
    if (total_size - asize >= MIN_BLOCK_SIZE) {
        /* Split the block */
        PUT(HDRP(bp), PACK(asize, 1));
        void *new_block = NEXT_BLKP(bp);
        PUT(HDRP(new_block), PACK(total_size - asize, 0));
        
        /* Initialize and insert the new free block */
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        insert_free_block(new_block);
    } else {
        /* Use the whole block */
        PUT(HDRP(bp), PACK(total_size, 1));
    }
}

/*
 * insert_free_block - Add a block to the free list (LIFO).
 */
static void insert_free_block(void *bp)
{
    SET_NEXT(bp, free_list_head);
    SET_PREV(bp, NULL);
    
    if (free_list_head != NULL) {
        SET_PREV(free_list_head, bp);
    }
    
    free_list_head = bp;
}

/*
 * remove_free_block - Remove a block from the free list.
 */
static void remove_free_block(void *bp)
{
    if (GET_PREV(bp) != NULL) {
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp) != NULL) {
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}