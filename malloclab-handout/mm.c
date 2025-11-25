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

/* Basic constants (Adjusted for 64-bit system, where size_t is 8 bytes) */
#define WSIZE 8        /* Header/Footer size (bytes) - assumed to be sizeof(size_t) */
#define DSIZE 16       /* Double word size and Alignment requirement */
#define CHUNKSIZE (1<<12)  /* Initial heap size (bytes) - 4KB */

/* Basic memory access macros */
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0xF) /* Use ~0xF to ensure 16-byte alignment */
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Block pointers */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* For free blocks: payload contains next/prev pointers */
#define FREE_BLOCK_PAYLOAD(bp) (bp)

/* Minimum block size: Header (WSIZE) + Footer (WSIZE) + 16-byte aligned payload (DSIZE for pointers) */
#define MIN_BLOCK_SIZE (DSIZE + DSIZE) // 32 bytes minimum

static void *free_list_head = NULL;

/* Helper functions */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);

/* Explicit Free List operations (Pointers are WSIZE apart) */
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

/* * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    debug_print("mm_init called", NULL);
    free_list_head = NULL;
    return 0;
}

/* * mm_malloc - Allocate a block from the free list.
 */
void *mm_malloc(size_t size)
{
    debug_print("mm_malloc called", NULL);
    if (DEBUG) printf("DEBUG: requested size = %zu\n", size);
    
    if (size == 0) return NULL;
    
    /* Adjust block size: payload size + Header (WSIZE) + Footer (WSIZE) */
    size_t asize = ALIGN(size + DSIZE); 
    if (asize < MIN_BLOCK_SIZE) asize = MIN_BLOCK_SIZE;
    
    if (DEBUG) printf("DEBUG: adjusted size = %zu\n", asize);
    
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
    size_t extendsize = MAX(asize, CHUNKSIZE);
    extendsize = PAGE_ALIGN(extendsize);
    
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
    
    /* Mark as free (Header and Footer) */
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    
    /* Insert into free list and coalesce */
    insert_free_block(ptr);
    coalesce(ptr);
}

/*
 * extend_heap - Extend heap with a new free block.
 */
static void *extend_heap(size_t size)
{
    char *start_addr;
    
    if (DEBUG) printf("DEBUG: mem_map requesting %zu bytes\n", size);
    if ((start_addr = mem_map(size)) == NULL)
        return NULL;
    
    if (DEBUG) printf("DEBUG: mem_map returned %p\n", start_addr);
    
    /* The payload (bp) must be 16-byte aligned.
     * HDRP(bp) must be WSIZE before bp. */
    char *bp = (char *)ALIGN((size_t)(start_addr + WSIZE));
    
    /* Calculate actual block size */
    size_t block_size = size - (bp - start_addr) + WSIZE; // Total mapped size - padding to align bp + WSIZE (header)
    block_size = ALIGN(block_size);  /* Ensure final size is DSIZE-aligned */
    
    /* Sanity check */
    if (block_size < MIN_BLOCK_SIZE) {
        if (DEBUG) printf("DEBUG: ERROR: block size %zu too small, need %zu\n", block_size, MIN_BLOCK_SIZE);
        mem_unmap(start_addr, size); // Release memory if not usable
        return NULL;
    }
    
    /* Set Header and Footer for the new free block */
    PUT(HDRP(bp), PACK(block_size, 0));
    PUT(FTRP(bp), PACK(block_size, 0));
    
    if (DEBUG) printf("DEBUG: set header at %p and footer at %p to size %zu\n", HDRP(bp), FTRP(bp), block_size);
    if (DEBUG) printf("DEBUG: payload at %p (aligned: %d)\n", bp, is_aligned(bp));
    
    /* Initialize free list pointers */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
    
    insert_free_block(bp);
    
    return bp;
}

/*
 * next_block - Get next block pointer (payload)
 */
static void *next_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    void *next = (char *)bp + size - DSIZE;
    return next;
}

/*
 * coalesce - Boundary tag coalescing
 */
static void *coalesce(void *bp)
{
    debug_print("coalesce called", bp);
    size_t size = GET_SIZE(HDRP(bp));
    
    /* Check previous block's allocation status using its footer, which is WSIZE before current header */
    size_t prev_alloc = GET_ALLOC(HDRP(bp) - WSIZE);
    
    /* Check next block's allocation status using its header */
    void *next_bp = next_block(bp);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    
    if (prev_alloc && next_alloc) {
        return bp; /* Case 1: No free blocks to coalesce */
    } 
    
    /* Remove the current block before any merging/re-insertion */
    remove_free_block(bp); 
    
    if (prev_alloc && !next_alloc) {         /* Case 2: Coalesce with next */
        debug_print("coalescing with next block", next_bp);
        remove_free_block(next_bp);
        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } 
    else if (!prev_alloc && next_alloc) {    /* Case 3: Coalesce with previous */
        /* Get previous block pointer (payload) using its footer size */
        size_t prev_size = GET_SIZE(HDRP(bp) - WSIZE);
        void *prev_bp = (char *)bp - prev_size;
        
        debug_print("coalescing with previous block", prev_bp);
        remove_free_block(prev_bp); // Should already be removed, but safe check
        size += prev_size;
        
        PUT(FTRP(bp), PACK(size, 0)); // Write new footer at current block's end
        PUT(HDRP(prev_bp), PACK(size, 0)); // Write new header at previous block's start
        bp = prev_bp;
    } 
    else {                                   /* Case 4: Coalesce all three */
        size_t prev_size = GET_SIZE(HDRP(bp) - WSIZE);
        void *prev_bp = (char *)bp - prev_size;
        
        debug_print("coalescing with previous and next blocks", next_bp);
        remove_free_block(next_bp);
        remove_free_block(prev_bp);
        
        size += prev_size + GET_SIZE(HDRP(next_bp));
        
        PUT(HDRP(prev_bp), PACK(size, 0)); // Write new header at start of previous
        PUT(FTRP(next_bp), PACK(size, 0)); // Write new footer at end of next
        bp = prev_bp;
    }
    
    /* Re-insert the new, larger block */
    insert_free_block(bp); 
    return bp;
}

/*
 * find_fit - First-fit search.
 */
static void *find_fit(size_t asize)
{
    debug_print("find_fit called", NULL);
    
    void *bp = free_list_head;
    while (bp != NULL) {
        size_t block_size = GET_SIZE(HDRP(bp));
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
    
    remove_free_block(bp);
    
    if (total_size >= asize + MIN_BLOCK_SIZE) {
        debug_print("splitting block", NULL);
        
        /* Set header/footer for the allocated part */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        /* New free block starts immediately after the allocated part */
        void *new_block = (char *)bp + asize;
        size_t remainder = total_size - asize;
        
        /* Set header/footer for the new free block */
        PUT(HDRP(new_block), PACK(remainder, 0));
        PUT(FTRP(new_block), PACK(remainder, 0));
        
        SET_NEXT(new_block, NULL);
        SET_PREV(new_block, NULL);
        
        insert_free_block(new_block);
        
    } else {
        debug_print("using whole block", NULL);
        /* Set header/footer for the whole block as allocated */
        PUT(HDRP(bp), PACK(total_size, 1));
        PUT(FTRP(bp), PACK(total_size, 1));
    }
}

/*
 * insert_free_block - Add to free list (LIFO).
 */
static void insert_free_block(void *bp)
{
    debug_print("insert_free_block", bp);
    
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
    debug_print("remove_free_block", bp);
    
    if (GET_PREV(bp) != NULL) {
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        free_list_head = GET_NEXT(bp);
    }
    
    if (GET_NEXT(bp) != NULL) {
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
    
    /* Clear pointers of the removed block to catch errors */
    SET_NEXT(bp, NULL);
    SET_PREV(bp, NULL);
}