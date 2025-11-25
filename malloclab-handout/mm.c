#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* Basic constants */
#define WSIZE 4        /* Word size (bytes) */
#define DSIZE 8        /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)  /* Initial heap size (bytes) */

#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Block header */
#define HDRP(bp) ((char *)(bp) - WSIZE)

/* Minimum block size */
#define MIN_BLOCK_SIZE (ALIGN(WSIZE + DSIZE))

static void *free_list_head = NULL;
static size_t total_heap_size = 0;

/* Helper functions */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* Free list operations */
#define GET_NEXT(bp) (*(void **)(bp))
#define GET_PREV(bp) (*(void **)((char *)(bp) + WSIZE))
#define SET_NEXT(bp, val) (*(void **)(bp) = (val))
#define SET_PREV(bp, val) (*(void **)((char *)(bp) + WSIZE) = (val))

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    free_list_head = NULL;
    total_heap_size = 0;
    return 0;
}

/* 
 * mm_malloc - Allocate a block from the free list.
 */
void *mm_malloc(size_t size)
{
    printf("DEBUG: mm_malloc(%zu)\n", size);  
    if (size == 0) return NULL;
    
    
    /* Adjust size with header and alignment */
    size_t asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK_SIZE) asize = MIN_BLOCK_SIZE;
    
    printf("DEBUG: looking for fit for size %zu\n", asize);
    void *bp;
    
    /* Search free list */
    if ((bp = find_fit(asize)) != NULL) {
        printf("DEBUG: found fit at %p\n", bp);
        place(bp, asize);
        return bp;
    }
    
    printf("DEBUG: no fit found, extending heap\n");
    /* Extend heap if no fit */
    size_t extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    if ((bp = extend_heap(extendsize)) == NULL)
        return NULL;
    
    printf("DEBUG: extended heap, new block at %p\n", bp);
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;
    
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));  /* Mark as free */
    
    insert_free_block(ptr);
    coalesce(ptr);
}

/*
 * extend_heap - Extend heap with a new free block.
 */
static void *extend_heap(size_t size)
{
    size_t asize = ALIGN(size);
    char *bp;
    
    if ((bp = mem_map(asize)) == NULL)
        return NULL;
    
    total_heap_size += asize;
    
    /* Initialize block header */
    PUT(bp, PACK(asize, 0));
    
    /* Initialize free list pointers */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    
    insert_free_block(bp);
    return bp;
}

/*
 * next_block - Get next block pointer safely
 */
static void *next_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    return (char *)bp + size;
}

/*
 * is_valid_block - Check if a block pointer is within heap bounds
 */
static int is_valid_block(void *bp) {
    return (bp != NULL);
}

/*
 * coalesce - Simple coalescing that only checks next block
 */
static void *coalesce(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void *next = next_block(bp);
    
    /* Only coalesce if next block exists and is free */
    if (is_valid_block(next) && !GET_ALLOC(HDRP(next))) {
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
        
        /* Re-insert coalesced block */
        SET_NEXT(bp, NULL);
        SET_PREV(bp, NULL);
        insert_free_block(bp);
    }
    
    return bp;
}

/*
 * find_fit - First-fit search.
 */
static void *find_fit(size_t asize)
{
    void *bp = free_list_head;
    while (bp != NULL) {
        if (GET_SIZE(HDRP(bp)) >= asize) {
            return bp;
        }
        bp = GET_NEXT(bp);
    }
    return NULL;
}

/*
 * place - Place block and split if possible.
 */
static void place(void *bp, size_t asize)
{
    size_t total_size = GET_SIZE(HDRP(bp));
    remove_free_block(bp);
    
    /* Only split if there's enough space for a new free block */
    if (total_size >= asize + MIN_BLOCK_SIZE) {
        /* Split block */
        PUT(HDRP(bp), PACK(asize, 1));
        
        void *new_block = next_block(bp);
        PUT(HDRP(new_block), PACK(total_size - asize, 0));
        
        /* Only add to free list if it's a valid free block */
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        insert_free_block(new_block);
        
        /* Try to coalesce the new free block with its neighbors */
        coalesce(new_block);
    } else {
        /* Use whole block */
        PUT(HDRP(bp), PACK(total_size, 1));
    }
}

/*
 * insert_free_block - Add to free list.
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
 * remove_free_block - Remove from free list.
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