#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* Debug flag - set to 1 to enable debug messages, 0 to disable */
#define DEBUG 1

/* always use 16-byte alignment */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

/* Basic constants */
#define WSIZE 4        /* Word size (bytes) */
#define DSIZE 8        /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)  /* Initial heap size (bytes) */

#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Block header - ensure payload is 16-byte aligned */
#define HDRP(bp) ((char *)(bp) - WSIZE)

/* For free blocks: payload contains next/prev pointers */
#define FREE_BLOCK_PAYLOAD(bp) (bp)

/* Minimum block size - header + 2 pointers, ensure 16-byte aligned */
#define MIN_BLOCK_SIZE (ALIGN(WSIZE + DSIZE))

static void *free_list_head = NULL;

/* Helper functions */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* Free list operations - these operate on the PAYLOAD of free blocks */
#define GET_NEXT(bp) (*(void **)(FREE_BLOCK_PAYLOAD(bp)))
#define GET_PREV(bp) (*(void **)((char *)FREE_BLOCK_PAYLOAD(bp) + WSIZE))
#define SET_NEXT(bp, val) (*(void **)(FREE_BLOCK_PAYLOAD(bp)) = (val))
#define SET_PREV(bp, val) (*(void **)((char *)FREE_BLOCK_PAYLOAD(bp) + WSIZE) = (val))

/* Debug function */
static void debug_print(const char *msg, void *bp) {
    if (DEBUG) {
        printf("DEBUG: %s", msg);
        if (bp) {
            size_t size = GET_SIZE(HDRP(bp));
            int alloc = GET_ALLOC(HDRP(bp));
            printf(" at %p, size=%zu, alloc=%d", bp, size, alloc);
            
            /* Sanity check - if size is ridiculous, something is wrong */
            if (size > 1000000) {
                printf(" [INVALID SIZE!]");
            }
        }
        printf("\n");
    }
}

/* Check if address is 16-byte aligned */
static int is_aligned(void *p) {
    return ((size_t)p & (ALIGNMENT-1)) == 0;
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    debug_print("mm_init called", NULL);
    free_list_head = NULL;
    return 0;
}

/* 
 * mm_malloc - Allocate a block from the free list.
 */
void *mm_malloc(size_t size)
{
    debug_print("mm_malloc called", NULL);
    if (DEBUG) printf("DEBUG: requested size = %zu\n", size);
    
    if (size == 0) return NULL;
    
    /* Adjust block size to include overhead and ensure 16-byte alignment */
    size_t asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK_SIZE) asize = MIN_BLOCK_SIZE;
    
    if (DEBUG) printf("DEBUG: adjusted size = %zu\n", asize);
    
    void *bp;
    
    /* Search free list */
    debug_print("searching free list", NULL);
    if ((bp = find_fit(asize)) != NULL) {
        debug_print("found fit", bp);
        place(bp, asize);
        if (DEBUG) printf("DEBUG: returning aligned payload: %p (aligned: %d)\n", bp, is_aligned(bp));
        return bp;
    }
    
    debug_print("no fit found, extending heap", NULL);
    /* Extend heap if no fit - ensure page alignment */
    size_t extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    extendsize = PAGE_ALIGN(extendsize + ALIGNMENT); /* Add padding and page align */
    if (DEBUG) printf("DEBUG: extending heap by %zu bytes\n", extendsize);
    
    if ((bp = extend_heap(extendsize)) == NULL)
        return NULL;
    
    debug_print("extended heap, new block", bp);
    place(bp, asize);
    debug_print("after place, returning", bp);
    if (DEBUG) printf("DEBUG: returning aligned payload: %p (aligned: %d)\n", bp, is_aligned(bp));
    return bp;
}

/*
 * mm_free - Free a block.
 */
void mm_free(void *ptr)
{
    debug_print("mm_free called", ptr);
    if (ptr == NULL) return;
    
    if (DEBUG) printf("DEBUG: freeing aligned payload: %p (aligned: %d)\n", ptr, is_aligned(ptr));
    
    size_t size = GET_SIZE(HDRP(ptr));
    if (DEBUG) printf("DEBUG: freeing block size=%zu\n", size);
    
    /* Clear the free list pointers first to avoid corruption */
    SET_NEXT(ptr, NULL);
    SET_PREV(ptr, NULL);
    
    /* Mark as free */
    PUT(HDRP(ptr), PACK(size, 0));
    
    insert_free_block(ptr);
    coalesce(ptr);
}

/*
 * extend_heap - Extend heap with a new free block.
 */
static void *extend_heap(size_t size)
{
    /* Request is already page-aligned from mm_malloc */
    size_t asize = size;
    char *bp;
    
    if (DEBUG) printf("DEBUG: mem_map requesting %zu bytes\n", asize);
    if ((bp = mem_map(asize)) == NULL)
        return NULL;
    
    if (DEBUG) printf("DEBUG: mem_map returned %p\n", bp);
    
    /* Find the first 16-byte aligned address for payload within the mapped memory */
    void *aligned_payload = (void *)ALIGN((size_t)(bp + WSIZE));
    
    /* Calculate header position (before the aligned payload) */
    void *header = (char *)aligned_payload - WSIZE;
    
    /* Calculate the actual usable size (from header to end of mapped memory) */
    size_t usable_size = asize - ((char *)header - bp);
    
    /* Make sure usable_size is aligned */
    usable_size = ALIGN(usable_size);
    
    /* Initialize block header */
    PUT(header, PACK(usable_size, 0));
    if (DEBUG) printf("DEBUG: set header at %p to size %zu, alloc 0\n", header, usable_size);
    if (DEBUG) printf("DEBUG: payload at %p (aligned: %d)\n", aligned_payload, is_aligned(aligned_payload));
    
    /* Initialize free list pointers in the payload */
    SET_NEXT(aligned_payload, NULL);
    SET_PREV(aligned_payload, NULL);
    if (DEBUG) printf("DEBUG: initialized free list pointers at payload %p\n", aligned_payload);
    
    insert_free_block(aligned_payload);
    return aligned_payload;
}

/*
 * next_block - Get next block pointer safely
 */
static void *next_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    void *next = (char *)bp + size;
    if (DEBUG) printf("DEBUG: next_block: %p + %zu = %p\n", bp, size, next);
    return next;
}

/*
 * coalesce - Simple coalescing with bounds checking
 */
static void *coalesce(void *bp)
{
    debug_print("coalesce called", bp);
    size_t size = GET_SIZE(HDRP(bp));
    
    /* Only coalesce with next block if it's reasonable */
    void *next = next_block(bp);
    
    /* Basic sanity check - next should be after current block */
    if (next <= bp) {
        if (DEBUG) printf("DEBUG: invalid next block, skipping coalesce\n");
        return bp;
    }
    
    if (DEBUG) printf("DEBUG: checking next block at %p\n", next);
    
    /* Only coalesce if next block exists and is free */
    if (!GET_ALLOC(HDRP(next))) {
        debug_print("coalescing with next block", next);
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
    debug_print("find_fit called", NULL);
    if (DEBUG) printf("DEBUG: looking for block >= %zu\n", asize);
    
    void *bp = free_list_head;
    while (bp != NULL) {
        size_t block_size = GET_SIZE(HDRP(bp));
        if (DEBUG) printf("DEBUG: checking block %p, size=%zu\n", bp, block_size);
        
        /* Sanity check - skip blocks with invalid sizes */
        if (block_size < MIN_BLOCK_SIZE || block_size > 1000000) {
            if (DEBUG) printf("DEBUG: skipping block with invalid size\n");
            bp = GET_NEXT(bp);
            continue;
        }
        
        if (block_size >= asize) {
            return bp;
        }
        bp = GET_NEXT(bp);
    }
    debug_print("no fit found", NULL);
    return NULL;
}

/*
 * place - Place block and split if possible.
 */
static void place(void *bp, size_t asize)
{
    debug_print("place called", bp);
    size_t total_size = GET_SIZE(HDRP(bp));
    
    if (DEBUG) printf("DEBUG: placing block of size %zu into block of size %zu\n", 
           asize, total_size);
    
    debug_print("removing from free list", bp);
    remove_free_block(bp);
    
    /* Only split if there's enough space for a new free block */
    if (total_size >= asize + MIN_BLOCK_SIZE) {
        if (DEBUG) printf("DEBUG: splitting block\n");
        
        /* Update current block header */
        PUT(HDRP(bp), PACK(asize, 1));
        
        /* Create new free block from remainder */
        void *new_block = (char *)bp + asize;
        size_t remainder = total_size - asize;
        
        if (DEBUG) printf("DEBUG: creating new free block at %p, size %zu\n", new_block, remainder);
        
        /* Initialize new block header */
        PUT(HDRP(new_block), PACK(remainder, 0));
        
        /* Initialize free list pointers for new block */
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        
        debug_print("inserting new free block", new_block);
        insert_free_block(new_block);
    } else {
        if (DEBUG) printf("DEBUG: using whole block\n");
        /* Use whole block */
        PUT(HDRP(bp), PACK(total_size, 1));
    }
}

/*
 * insert_free_block - Add to free list.
 */
static void insert_free_block(void *bp)
{
    debug_print("insert_free_block", bp);
    if (DEBUG) printf("DEBUG: current free_list_head = %p\n", free_list_head);
    
    SET_NEXT(bp, free_list_head);
    SET_PREV(bp, NULL);
    
    if (free_list_head != NULL) {
        SET_PREV(free_list_head, bp);
    }
    
    free_list_head = bp;
    if (DEBUG) printf("DEBUG: new free_list_head = %p\n", free_list_head);
}

/*
 * remove_free_block - Remove from free list.
 */
static void remove_free_block(void *bp)
{
    debug_print("remove_free_block", bp);
    
    if (GET_PREV(bp) != NULL) {
        if (DEBUG) printf("DEBUG: updating prev->next from %p to %p\n", bp, GET_NEXT(bp));
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        if (DEBUG) printf("DEBUG: updating free_list_head from %p to %p\n", free_list_head, GET_NEXT(bp));
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp) != NULL) {
        if (DEBUG) printf("DEBUG: updating next->prev from %p to %p\n", bp, GET_PREV(bp));
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}