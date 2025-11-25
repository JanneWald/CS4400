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

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

/* Basic constants and macros */
#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Initial heap size (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Free list pointers - for free blocks only */
#define GET_NEXT(bp) (*(void **)(bp))
#define GET_PREV(bp) (*(void **)((char *)(bp) + WSIZE))
#define SET_NEXT(bp, val) (GET_NEXT(bp) = (val))
#define SET_PREV(bp, val) (GET_PREV(bp) = (val))

/* Minimum block size - header + next/prev pointers + alignment */
#define MIN_BLOCK_SIZE (ALIGN(WSIZE + DSIZE + DSIZE))

static void *free_list_head = NULL;
static void *heap_start = NULL;

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
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
    /* Create the initial empty heap */
    if ((heap_start = mem_map(CHUNKSIZE)) == NULL)
        return -1;
    
    /* Initialize free list head */
    free_list_head = NULL;
    
    /* Initialize the heap with a prologue and epilogue block */
    size_t start_size = CHUNKSIZE - 2*DSIZE;
    
    /* Prologue footer (not used but for alignment) */
    PUT(heap_start, 0);
    
    /* Initial free block covering almost entire heap */
    PUT(heap_start + WSIZE, PACK(start_size, 0)); /* Header */
    
    /* Set up epilogue header */
    PUT(heap_start + CHUNKSIZE - WSIZE, PACK(0, 1));
    
    /* Initialize the free block with next/prev pointers */
    void *free_block = heap_start + DSIZE;
    SET_NEXT(free_block, NULL);
    SET_PREV(free_block, NULL);
    
    /* Add to free list */
    insert_free_block(free_block);
    
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
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
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
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    size = PAGE_ALIGN(size); /* Align to page size */
    
    if ((bp = mem_map(size)) == NULL)
        return NULL;
    
    /* Initialize free block header */
    PUT(HDRP(bp), PACK(size, 0));
    
    /* New epilogue header */
    PUT(HDRP(bp) + size, PACK(0, 1));
    
    /* Initialize free block pointers */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    
    /* Add to free list */
    insert_free_block(bp);
    
    /* Coalesce if the previous block is free */
    return coalesce(bp);
}

/*
 * coalesce - Boundary tag coalescing.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp))) || (PREV_BLKP(bp) < heap_start);
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc) { /* Case 1 */
        return bp;
    }
    else if (prev_alloc && !next_alloc) { /* Case 2 */
        remove_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) { /* Case 3 */
        remove_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else { /* Case 4 */
        remove_free_block(PREV_BLKP(bp));
        remove_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    
    /* Update free list for coalesced block */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    insert_free_block(bp);
    
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
    size_t remainder = total_size - asize;
    
    remove_free_block(bp);
    
    if (remainder >= MIN_BLOCK_SIZE) {
        /* Split the block */
        PUT(HDRP(bp), PACK(asize, 1));
        void *new_block = NEXT_BLKP(bp);
        PUT(HDRP(new_block), PACK(remainder, 0));
        
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
    if (free_list_head != NULL) {
        SET_NEXT(bp, free_list_head);
        SET_PREV(free_list_head, bp);
        SET_PREV(bp, NULL);
    } else {
        SET_NEXT(bp, NULL);
        SET_PREV(bp, NULL);
    }
    free_list_head = bp;
}

/*
 * remove_free_block - Remove a block from the free list.
 */
static void remove_free_block(void *bp)
{
    if (GET_PREV(bp)) {
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp)) {
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}