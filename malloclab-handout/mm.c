/*
 * mm.c - An Explicit Free List Allocator implementation using a First-Fit policy.
 * * This allocator manages the heap using doubly-linked free blocks, which
 * are coalesced upon freeing to prevent fragmentation. It uses a prologue
 * and epilogue block to simplify boundary conditions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: DO NOT MODIFY ANYTHING BELOW THIS LINE
 *********************************************************/

/* always use 16-byte alignment */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))


/*********************************************************
 * EXPLICIT LIST CONSTANTS AND MACROS
 *********************************************************/

#define WSIZE 8          /* Word size (bytes) */
#define DSIZE 16         /* Double word size (alignment/min block metadata) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (4KB) */

/* Minimum block size: Header (8) + Footer (8) + 2 Pointers (16) = 32 bytes */
#define MIN_BLOCK_SIZE (2 * WSIZE + 2 * DSIZE) 

/* Read/Write a word at address p */
#define GET(p)       (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))

/* Combine a size and alloc bit */
#define PACK(size, alloc) ((size) | (alloc))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0xF)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given a block pointer bp, compute address of its header and footer */
#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks (sequential heap blocks) */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))


/*********************************************************
 * EXPLICIT LIST POINTER MANIPULATION
 * Pointers are stored at the beginning of the free block payload
 *********************************************************/

typedef struct block_free_s {
    size_t size_status;     /* Header */
    struct block_free_s *prev; /* Explicit list: previous free block pointer (WSIZE offset) */
    struct block_free_s *next; /* Explicit list: next free block pointer (DSIZE offset) */
    /* Footer follows */
} block_free_t;

/* Get the explicit list PREV pointer from a free block's payload */
#define GET_PREV_PTR(bp) (*(block_free_t **)(bp))

/* Get the explicit list NEXT pointer from a free block's payload */
#define GET_NEXT_PTR(bp) (*(block_free_t **)((char *)(bp) + WSIZE))

/* Set the explicit list PREV pointer in a free block's payload */
#define SET_PREV_PTR(bp, val) (GET_PREV_PTR(bp) = val)

/* Set the explicit list NEXT pointer in a free block's payload */
#define SET_NEXT_PTR(bp, val) (GET_NEXT_PTR(bp) = val)


/*********************************************************
 * GLOBAL VARIABLES and FUNCTION PROTOTYPES
 *********************************************************/

/* Global pointer to the head of the explicit free list */
static void *free_list_head = NULL;
/* Global pointer to the start of the heap/prologue padding */
static void *heap_listp = NULL; 

static void *extend_heap(size_t size);
static void insert_block(void *bp);
static void remove_block(void *bp);
static void *find_fit(size_t adjusted_size);
static void place(void *bp, size_t adjusted_size);
static void *coalesce(void *bp);


/*********************************************************
 * EXPLICIT LIST UTILITY FUNCTIONS
 *********************************************************/

/*
 * insert_block - Insert a block at the head of the explicit free list (LIFO)
 */
static void insert_block(void *bp) {
    // New block's next points to the current head (could be NULL)
    SET_NEXT_PTR(bp, free_list_head);
    
    // New block's previous is NULL, as it's the new head
    SET_PREV_PTR(bp, NULL);

    // If the list wasn't empty, update the old head's PREV pointer
    if (free_list_head != NULL) {
        SET_PREV_PTR(free_list_head, bp);
    }

    // Update the global head pointer
    free_list_head = bp;
}

/*
 * remove_block - Remove a block from the explicit free list
 */
static void remove_block(void *bp) {
    void *prev_block = GET_PREV_PTR(bp);
    void *next_block = GET_NEXT_PTR(bp);

    // Case 1: If the block is the head of the list
    if (prev_block == NULL) {
        // Make the next block the new head
        free_list_head = next_block;
    } 
    // Case 2: Block is in the middle or end
    else {
        // The previous block bypasses the current block
        SET_NEXT_PTR(prev_block, next_block);
    }

    // Case 3: If the block is NOT the tail of the list
    if (next_block != NULL) {
        // The next block's PREV pointer points to the previous block (bypassing the current block)
        SET_PREV_PTR(next_block, prev_block);
    }
}


/*********************************************************
 * CORE MALLOC FUNCTIONS
 *********************************************************/

/* * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    // Reset global state
    free_list_head = NULL;

    // Create the initial empty heap (Padding + Prologue Header + Prologue Footer + Epilogue Header)
    // We need 4 words total (4 * WSIZE = 32 bytes)
    size_t initial_size = PAGE_ALIGN(4 * WSIZE); 
    if ((heap_listp = mem_map(initial_size)) == NULL) {
        return -1;
    }

    // heap_listp points to the padding word
    // 1. Alignment padding (unused, but ensures first payload is aligned)
    PUT(heap_listp, 0); 
    // 2. Prologue Header (Size = DSIZE, Allocated = 1)
    PUT(HDRP(heap_listp + WSIZE), PACK(DSIZE, 1)); 
    // 3. Prologue Footer
    PUT(FTRP(heap_listp + WSIZE), PACK(DSIZE, 1));
    // 4. Epilogue Header (Size = 0, Allocated = 1) - Marks the end of the heap
    PUT(HDRP(NEXT_BLKP(heap_listp + WSIZE)), PACK(0, 1));
    
    // The first actual block starts after the prologue.
    void *bp = NEXT_BLKP(heap_listp + WSIZE);

    // Extend the heap to create an initial large free block
    if (extend_heap(CHUNKSIZE) == NULL) {
        return -1;
    }
    
    return 0;
}

/*
 * mm_malloc - Allocate a block by using bytes from the explicit free list,
 * grabbing a new page if necessary.
 */
void *mm_malloc(size_t size) {
    size_t adjusted_size; // Adjusted block size
    size_t extendsize;    // Amount to extend heap if no fit
    void *bp;             // Pointer to the block found

    // Ignore spurious requests
    if (size == 0) return NULL;

    // Calculate adjusted block size (16-byte alignment + header/footer)
    // DSIZE for Header + Footer
    adjusted_size = ALIGN(size + DSIZE); 
    
    // Ensure the block is large enough to store the free list pointers (MIN_BLOCK_SIZE)
    if (adjusted_size < MIN_BLOCK_SIZE) {
        adjusted_size = MIN_BLOCK_SIZE;
    }

    // 1. Search the explicit free list for a fit (First-Fit)
    if ((bp = find_fit(adjusted_size)) != NULL) {
        place(bp, adjusted_size);
        return bp;
    }

    // 2. No fit found. Extend heap.
    extendsize = PAGE_ALIGN(adjusted_size);
    if ((bp = extend_heap(extendsize)) == NULL) {
        return NULL;
    }
    
    // Allocate the block from the newly extended space
    place(bp, adjusted_size);
    return bp;
}

/*
 * mm_free - Freeing a block, coalescing, and adding to the explicit free list.
 */
void mm_free(void *bp) {
    if (bp == NULL) return;

    size_t size = GET_SIZE(HDRP(bp));

    // Mark the block as free in header and footer
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    // Coalesce with neighbors and insert the resulting block into the free list
    coalesce(bp);

    // NOTE: mem_unmap optimization goes here if needed later.
}


/*********************************************************
 * STATIC HELPER IMPLEMENTATIONS
 *********************************************************/

/* * extend_heap - Request more memory by calling mem_map
 * Initializes the new chunk of memory as a free block.
 */
static void *extend_heap(size_t size) {
    void *bp;
    size_t extended_size = PAGE_ALIGN(size);

    // Request new pages
    if ((bp = mem_map(extended_size)) == NULL) {
        return NULL;
    }

    // Initialize new free block header/footer
    PUT(HDRP(bp), PACK(extended_size, 0)); 
    PUT(FTRP(bp), PACK(extended_size, 0));
    
    // New epilogue header (marks the new end of the heap)
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    // Coalesce with the previous block (which was the old epilogue's neighbor)
    // and insert into the free list.
    return coalesce(bp);
}

/* * find_fit - First-Fit search: Iterate through the explicit free list
 */
static void *find_fit(size_t adjusted_size) {
    void *bp = free_list_head;

    while (bp != NULL) {
        if (GET_SIZE(HDRP(bp)) >= adjusted_size) {
            return bp; // Found a fit!
        }
        // Move to the next free block using the explicit list pointer
        bp = GET_NEXT_PTR(bp);
    }
    return NULL; // No fit found
}

/* * place - Allocates the requested block and handles splitting
 */
static void place(void *bp, size_t adjusted_size) {
    size_t total_size = GET_SIZE(HDRP(bp));
    size_t remainder = total_size - adjusted_size;

    // First, remove the block from the free list
    remove_block(bp);

    // Check if the remainder is large enough to be a new free block (>= MIN_BLOCK_SIZE)
    if (remainder >= MIN_BLOCK_SIZE) {
        // A. Split the block
        
        // 1. Allocate the requested portion
        PUT(HDRP(bp), PACK(adjusted_size, 1));
        PUT(FTRP(bp), PACK(adjusted_size, 1));

        // 2. Create the new free block
        void *new_free_bp = NEXT_BLKP(bp);
        PUT(HDRP(new_free_bp), PACK(remainder, 0));
        PUT(FTRP(new_free_bp), PACK(remainder, 0));

        // 3. Add the new free block to the explicit list
        insert_block(new_free_bp);
    } else {
        // B. Allocate the entire block (no split)
        PUT(HDRP(bp), PACK(total_size, 1));
        PUT(FTRP(bp), PACK(total_size, 1));
    }
}

/* * coalesce - Coalesce a free block with adjacent free neighbors.
 * Returns pointer to the start of the new coalesced block.
 */
static void *coalesce(void *bp) {
    // Check if previous block is allocated (using its footer)
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    // Check if next block is allocated (using its header)
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    void *prev_blkp = PREV_BLKP(bp);
    void *next_blkp = NEXT_BLKP(bp);

    // Case 1: Both neighbors are allocated (no coalescing)
    if (prev_alloc && next_alloc) {
        // Do nothing with neighbors
    } 
    // Case 2: Only next block is free
    else if (prev_alloc && !next_alloc) {
        // Remove the next free block from the explicit list
        remove_block(next_blkp); 
        
        // Merge sizes
        size += GET_SIZE(HDRP(next_blkp));
        
        // Update header of current block and footer of next block (bp stays the same)
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } 
    // Case 3: Only previous block is free
    else if (!prev_alloc && next_alloc) {
        // Remove the previous free block from the explicit list
        remove_block(prev_blkp);
        
        // Merge sizes
        size += GET_SIZE(HDRP(prev_blkp));
        
        // bp now points to the previous block's payload start (moves backward)
        bp = prev_blkp;
        
        // Update header/footer of the new merged block
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } 
    // Case 4: Both previous and next blocks are free
    else {
        // Remove both neighbors from the explicit list
        remove_block(prev_blkp);
        remove_block(next_blkp);
        
        // Merge all three sizes
        size += GET_SIZE(HDRP(prev_blkp)) + GET_SIZE(HDRP(next_blkp));
        
        // bp now points to the previous block's payload start (moves backward)
        bp = prev_blkp;

        // Update header/footer of the new merged block
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    // Add the resulting coalesced block (bp) back into the free list
    insert_block(bp);

    return bp;
}