/*
 * mm.c - An Explicit Free List Allocator implementation using a First-Fit policy.
 * * This version includes a toggleable debug feature to help pinpoint pointer errors.
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
    struct block_free_s *prev; /* Explicit list: previous free block pointer */
    struct block_free_s *next; /* Explicit list: next free block pointer */
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
 * GLOBAL VARIABLES and DEBUGGING
 *********************************************************/

/* Global pointer to the head of the explicit free list */
static void *free_list_head = NULL;
/* Global pointer to the start of the heap/prologue padding */
static void *heap_listp = NULL; 

/* Set this to 1 to enable debug output, 0 otherwise */
static int mm_debug = 1; 

/* Conditional debug print macro */
#define dbg_printf(...) \
    if (mm_debug) { printf(__VA_ARGS__); }

/*********************************************************
 * FUNCTION PROTOTYPES
 *********************************************************/

static void *extend_heap(size_t size);
static void insert_block(void *bp);
static void remove_block(void *bp);
static void *find_fit(size_t adjusted_size);
static void place(void *bp, size_t adjusted_size);
static void *coalesce(void *bp);
static void print_heap_state();


/*********************************************************
 * EXPLICIT LIST UTILITY FUNCTIONS
 *********************************************************/

/*
 * insert_block - Insert a block at the head of the explicit free list (LIFO)
 */
static void insert_block(void *bp) {
    dbg_printf("INSERT: Inserting block %p (Size: %lu)\n", bp, GET_SIZE(HDRP(bp)));

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

    dbg_printf("INSERT: New head is %p\n", free_list_head);
}

/*
 * remove_block - Remove a block from the explicit free list
 */
static void remove_block(void *bp) {
    void *prev_block = GET_PREV_PTR(bp);
    void *next_block = GET_NEXT_PTR(bp);

    dbg_printf("REMOVE: Removing block %p (Size: %lu). Prev: %p, Next: %p\n", 
               bp, GET_SIZE(HDRP(bp)), prev_block, next_block);

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

    dbg_printf("REMOVE: List state updated. New head: %p\n", free_list_head);
}

/*
 * print_heap_state - Helper function to print the current state of the explicit list.
 */
static void print_heap_state() {
    if (!mm_debug) return;
    
    void *bp = heap_listp;
    printf("\n--- HEAP DUMP ---\n");
    printf("Head: %p, Start: %p, Size: %lu\n", free_list_head, heap_listp, mem_heapsize());
    
    // Traverse the entire heap (implicit traversal)
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        size_t size = GET_SIZE(HDRP(bp));
        int alloc = GET_ALLOC(HDRP(bp));
        
        printf("Block %p: Size=%lu, Alloc=%d, Header=%lu", bp, size, alloc, GET(HDRP(bp)));

        if (!alloc) {
            printf(", PrevFree=%p, NextFree=%p", 
                   GET_PREV_PTR(bp), GET_NEXT_PTR(bp));
        }
        printf("\n");
    }
    printf("Epilogue: %p\n", bp);
    printf("-----------------\n\n");
}


/*********************************************************
 * CORE MALLOC FUNCTIONS
 *********************************************************/

/* * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    dbg_printf("\n*** mm_init called ***\n");

    // Reset global state
    free_list_head = NULL;

    // Create the initial empty heap (Padding + Prologue Header + Prologue Footer + Epilogue Header)
    size_t initial_size = PAGE_ALIGN(4 * WSIZE); 
    if ((heap_listp = mem_map(initial_size)) == NULL) {
        return -1;
    }
    dbg_printf("INIT: Mapped initial %lu bytes at %p\n", initial_size, heap_listp);

    // heap_listp points to the padding word
    // 1. Alignment padding 
    PUT(heap_listp, 0); 
    // 2. Prologue Header (Size = DSIZE, Allocated = 1)
    PUT(HDRP(heap_listp + WSIZE), PACK(DSIZE, 1)); 
    // 3. Prologue Footer
    PUT(FTRP(heap_listp + WSIZE), PACK(DSIZE, 1));
    // 4. Epilogue Header (Size = 0, Allocated = 1) - Marks the end of the heap
    PUT(HDRP(NEXT_BLKP(heap_listp + WSIZE)), PACK(0, 1));
    
    // Extend the heap to create an initial large free block
    if (extend_heap(CHUNKSIZE) == NULL) {
        return -1;
    }
    
    print_heap_state();
    return 0;
}

/*
 * mm_malloc - Allocate a block.
 */
void *mm_malloc(size_t size) {
    size_t adjusted_size; // Adjusted block size
    size_t extendsize;    // Amount to extend heap if no fit
    void *bp;             // Pointer to the block found

    dbg_printf("\n--- mm_malloc(%lu) called ---\n", size);

    // Ignore spurious requests
    if (size == 0) return NULL;

    // Calculate adjusted block size 
    adjusted_size = ALIGN(size + DSIZE); 
    
    // Ensure the block is large enough to store the free list pointers (MIN_BLOCK_SIZE)
    if (adjusted_size < MIN_BLOCK_SIZE) {
        adjusted_size = MIN_BLOCK_SIZE;
    }

    // 1. Search the explicit free list for a fit (First-Fit)
    if ((bp = find_fit(adjusted_size)) != NULL) {
        dbg_printf("MALLOC: Found fit %p for size %lu\n", bp, adjusted_size);
        place(bp, adjusted_size);
        print_heap_state();
        return bp;
    }

    // 2. No fit found. Extend heap.
    extendsize = PAGE_ALIGN(adjusted_size);
    dbg_printf("MALLOC: No fit found. Extending heap by %lu\n", extendsize);
    if ((bp = extend_heap(extendsize)) == NULL) {
        return NULL;
    }
    
    // Allocate the block from the newly extended space
    place(bp, adjusted_size);
    print_heap_state();
    return bp;
}

/*
 * mm_free - Freeing a block.
 */
void mm_free(void *bp) {
    if (bp == NULL) return;

    dbg_printf("\n--- mm_free(%p) called ---\n", bp);
    print_heap_state();

    size_t size = GET_SIZE(HDRP(bp));

    // Mark the block as free in header and footer
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    dbg_printf("FREE: Block %p marked free (Size: %lu). Coalescing...\n", bp, size);

    // Coalesce with neighbors and insert the resulting block into the free list
    coalesce(bp);

    print_heap_state();
}


/*********************************************************
 * STATIC HELPER IMPLEMENTATIONS
 *********************************************************/

/* * extend_heap - Request more memory by calling mem_map
 */
static void *extend_heap(size_t size) {
    void *bp;
    size_t extended_size = PAGE_ALIGN(size);

    dbg_printf("EXTEND: Calling mem_map for %lu bytes.\n", extended_size);
    if ((bp = mem_map(extended_size)) == NULL) {
        return NULL;
    }

    // Initialize new free block header/footer
    PUT(HDRP(bp), PACK(extended_size, 0)); 
    PUT(FTRP(bp), PACK(extended_size, 0));
    
    // New epilogue header 
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    dbg_printf("EXTEND: New block at %p, size %lu. Coalescing.\n", bp, extended_size);

    // Coalesce and insert into the free list.
    return coalesce(bp);
}

/* * find_fit - First-Fit search
 */
static void *find_fit(size_t adjusted_size) {
    void *bp = free_list_head;

    dbg_printf("FIND_FIT: Searching for block of size %lu\n", adjusted_size);

    while (bp != NULL) {
        dbg_printf("FIND_FIT: Checking block %p (Size: %lu, Alloc: %d)\n", 
                   bp, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));

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

    dbg_printf("PLACE: Placing %lu in block %p of size %lu. Remainder: %lu\n", 
               adjusted_size, bp, total_size, remainder);

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
        dbg_printf("PLACE: Block %p split. New free block at %p (Size: %lu)\n", bp, new_free_bp, remainder);

        // 3. Add the new free block to the explicit list
        insert_block(new_free_bp);
    } else {
        // B. Allocate the entire block (no split)
        PUT(HDRP(bp), PACK(total_size, 1));
        PUT(FTRP(bp), PACK(total_size, 1));
        dbg_printf("PLACE: Allocated entire block %p (Size: %lu)\n", bp, total_size);
    }
}

/* * coalesce - Coalesce a free block with adjacent free neighbors.
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
        dbg_printf("COALESCE: Case 1. No coalescing.\n");
        // bp stays the same
    } 
    // Case 2: Only next block is free
    else if (prev_alloc && !next_alloc) {
        dbg_printf("COALESCE: Case 2. Coalesce with next block %p.\n", next_blkp);

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
        dbg_printf("COALESCE: Case 3. Coalesce with previous block %p.\n", prev_blkp);
        
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
        dbg_printf("COALESCE: Case 4. Coalesce with both prev %p and next %p.\n", prev_blkp, next_blkp);

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
    
    dbg_printf("COALESCE: Final merged block %p, Size: %lu. Inserting into list.\n", bp, size);
    
    // Add the resulting coalesced block (bp) back into the free list
    insert_block(bp);

    return bp;
}