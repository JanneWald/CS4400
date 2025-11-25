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

/* Debug function */
static void debug_print(const char *msg, void *bp) {
    printf("DEBUG: %s", msg);
    if (bp) {
        printf(" at %p, size=%zu, alloc=%d", bp, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
    }
    printf("\n");
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
    printf("DEBUG: requested size = %zu\n", size);
    
    if (size == 0) return NULL;
    
    /* Adjust size with header and alignment */
    size_t asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK_SIZE) asize = MIN_BLOCK_SIZE;
    
    printf("DEBUG: adjusted size = %zu\n", asize);
    
    void *bp;
    
    /* Search free list */
    debug_print("searching free list", NULL);
    if ((bp = find_fit(asize)) != NULL) {
        debug_print("found fit", bp);
        place(bp, asize);
        return bp;
    }
    
    debug_print("no fit found, extending heap", NULL);
    /* Extend heap if no fit */
    size_t extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    printf("DEBUG: extending heap by %zu bytes\n", extendsize);
    
    if ((bp = extend_heap(extendsize)) == NULL)
        return NULL;
    
    debug_print("extended heap, new block", bp);
    place(bp, asize);
    debug_print("after place, returning", bp);
    return bp;
}

/*
 * mm_free - Free a block.
 */
void mm_free(void *ptr)
{
    debug_print("mm_free called", ptr);
    if (ptr == NULL) return;
    
    size_t size = GET_SIZE(HDRP(ptr));
    printf("DEBUG: freeing block size=%zu\n", size);
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
    
    printf("DEBUG: mem_map requesting %zu bytes\n", asize);
    if ((bp = mem_map(asize)) == NULL)
        return NULL;
    
    printf("DEBUG: mem_map returned %p\n", bp);
    
    /* Initialize block header */
    PUT(bp, PACK(asize, 0));
    printf("DEBUG: set header at %p to size %zu, alloc 0\n", bp, asize);
    
    /* Initialize free list pointers */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    printf("DEBUG: initialized free list pointers\n");
    
    insert_free_block(bp);
    return bp;
}

/*
 * next_block - Get next block pointer safely
 */
static void *next_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    void *next = (char *)bp + size;
    printf("DEBUG: next_block: %p + %zu = %p\n", bp, size, next);
    return next;
}

/*
 * coalesce - Simple coalescing that only checks next block
 */
static void *coalesce(void *bp)
{
    debug_print("coalesce called", bp);
    size_t size = GET_SIZE(HDRP(bp));
    void *next = next_block(bp);
    
    printf("DEBUG: checking next block at %p\n", next);
    
    /* Only coalesce if next block exists and is free */
    if (next != NULL && !GET_ALLOC(HDRP(next))) {
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
    printf("DEBUG: looking for block >= %zu\n", asize);
    
    void *bp = free_list_head;
    while (bp != NULL) {
        printf("DEBUG: checking block %p, size=%zu\n", bp, GET_SIZE(HDRP(bp)));
        if (GET_SIZE(HDRP(bp)) >= asize) {
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
    printf("DEBUG: placing block of size %zu into block of size %zu\n", 
           asize, GET_SIZE(HDRP(bp)));
    
    size_t total_size = GET_SIZE(HDRP(bp));
    
    debug_print("removing from free list", bp);
    remove_free_block(bp);
    
    /* Only split if there's enough space for a new free block */
    if (total_size >= asize + MIN_BLOCK_SIZE) {
        printf("DEBUG: splitting block\n");
        /* Split block */
        PUT(HDRP(bp), PACK(asize, 1));
        
        void *new_block = next_block(bp);
        size_t remainder = total_size - asize;
        printf("DEBUG: creating new free block at %p, size %zu\n", new_block, remainder);
        
        PUT(HDRP(new_block), PACK(remainder, 0));
        
        /* Initialize free list pointers for new block */
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        
        debug_print("inserting new free block", new_block);
        insert_free_block(new_block);
    } else {
        printf("DEBUG: using whole block\n");
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
    printf("DEBUG: current free_list_head = %p\n", free_list_head);
    
    SET_NEXT(bp, free_list_head);
    SET_PREV(bp, NULL);
    
    if (free_list_head != NULL) {
        SET_PREV(free_list_head, bp);
    }
    
    free_list_head = bp;
    printf("DEBUG: new free_list_head = %p\n", free_list_head);
}

/*
 * remove_free_block - Remove from free list.
 */
static void remove_free_block(void *bp)
{
    debug_print("remove_free_block", bp);
    
    if (GET_PREV(bp) != NULL) {
        printf("DEBUG: updating prev->next from %p to %p\n", GET_NEXT(bp), GET_NEXT(GET_PREV(bp)));
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        printf("DEBUG: updating free_list_head from %p to %p\n", free_list_head, GET_NEXT(bp));
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp) != NULL) {
        printf("DEBUG: updating next->prev from %p to %p\n", GET_PREV(bp), GET_PREV(GET_NEXT(bp)));
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}