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

/* Block header */
#define HDRP(bp) ((char *)(bp) - WSIZE)

/* For free blocks: payload contains next/prev pointers */
#define FREE_BLOCK_PAYLOAD(bp) (bp)

/* Minimum block size - header + 2 pointers */
#define MIN_BLOCK_SIZE (ALIGN(WSIZE + DSIZE))

static void *free_list_head = NULL;

/* Helper functions */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* Free list operations */
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
        }
        printf("\n");
    }
}

/* Check if address is 16-byte aligned */
static int is_aligned(void *p) {
    return ((size_t)p & (ALIGNMENT-1)) == 0;
}

/* Simple MAX macro */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

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
    /* Extend heap if no fit */
    size_t extendsize = MAX(asize, CHUNKSIZE);
    extendsize = PAGE_ALIGN(extendsize);
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
    
    /* Mark as free */
    PUT(HDRP(ptr), PACK(size, 0));
    
    /* Initialize free list pointers */
    SET_NEXT(ptr, NULL);
    SET_PREV(ptr, NULL);
    
    insert_free_block(ptr);
    coalesce(ptr);
}

/*
 * extend_heap - Extend heap with a new free block.
 */
static void *extend_heap(size_t size)
{
    char *bp;
    
    if (DEBUG) printf("DEBUG: mem_map requesting %zu bytes\n", size);
    if ((bp = mem_map(size)) == NULL)
        return NULL;
    
    if (DEBUG) printf("DEBUG: mem_map returned %p\n", bp);
    
    /* SIMPLE FIX: Put header at start, payload right after */
    char *header = bp;
    char *payload = bp + WSIZE;
    
    /* Ensure payload is 16-byte aligned */
    payload = (char *)ALIGN((size_t)payload);
    
    /* Calculate actual block size from aligned payload to end */
    size_t block_size = size - (payload - bp);
    
    /* Align the block size */
    block_size = ALIGN(block_size);
    
    /* Set the header */
    PUT(header, PACK(block_size, 0));
    
    if (DEBUG) printf("DEBUG: set header at %p to size %zu\n", header, block_size);
    if (DEBUG) printf("DEBUG: payload at %p (aligned: %d)\n", payload, is_aligned(payload));
    
    /* Initialize free list pointers */
    SET_NEXT(payload, NULL);
    SET_PREV(payload, NULL);
    
    insert_free_block(payload);
    return payload;
}

/*
 * next_block - Get next block pointer
 */
static void *next_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    void *next = (char *)bp + size;
    if (DEBUG) printf("DEBUG: next_block: %p + %zu = %p\n", bp, size, next);
    return next;
}

/*
 * coalesce - Boundary tag coalescing
 */
static void *coalesce(void *bp)
{
    debug_print("coalesce called", bp);
    size_t size = GET_SIZE(HDRP(bp));
    int coalesced = 0;
    
    /* Coalesce with next block if it exists and is free */
    void *next = next_block(bp);
    if (!GET_ALLOC(HDRP(next))) {
        debug_print("coalescing with next block", next);
        remove_free_block(next);
        size += GET_SIZE(HDRP(next));
        coalesced = 1;
    }
    
    if (coalesced) {
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
    
    remove_free_block(bp);
    
    if (total_size >= asize + MIN_BLOCK_SIZE) {
        if (DEBUG) printf("DEBUG: splitting block\n");
        
        /* Split the block */
        PUT(HDRP(bp), PACK(asize, 1));
        
        void *new_block = (char *)bp + asize;
        size_t remainder = total_size - asize;
        
        if (DEBUG) printf("DEBUG: new free block at %p, size %zu\n", new_block, remainder);
        
        PUT(HDRP(new_block), PACK(remainder, 0));
        
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        insert_free_block(new_block);
        
    } else {
        if (DEBUG) printf("DEBUG: using whole block\n");
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
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp) != NULL) {
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}