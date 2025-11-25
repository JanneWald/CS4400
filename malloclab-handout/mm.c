/*
 * mm-naive.c - The least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by allocating a
 * new page as needed.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused.
 *
 * For my approach I decided to use an explicit free list (implemented via a doubly
 * linked list) for keeping track of free blocks. A block itself is header + payload + footer.
 * When allocating a block we check if there's an available size in the free list, if there
 * is then we remove that block from the list. Blocks are coalesced and split when possible.
 * Pages also get unmapped when it can be.
 *
 */
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

#define BLOCK_OVERHEAD 16

// This assumes you have a struct or typedef called "block_header" and "block_footer"
#define OVERHEAD (sizeof(block_header)+sizeof(block_footer))

// Given a payload pointer, get the header or footer pointer
#define HDRP(bp) ((char *)(bp) - sizeof(block_header))
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp))-OVERHEAD)

// Given a payload pointer, get the next or previous payload pointer
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE((char*)(bp)-OVERHEAD))

// ******These macros assume you are using a size_t for headers and footers******
//
// Given a pointer to a header, get or set its value
#define GET(p) (*(size_t*)(p))
#define PUT(p, val) (*(size_t*)(p) = (val))

// Combine a size and alloc bit
#define PACK(size, alloc) ((size) | (alloc))

// Given a header pointer, get the alloc or size
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~0xF)

#define MAX(x, y) ((x) > (y) ? (x):(y))

// explicit free list
typedef struct list_node{
    struct list_node *next;
    struct list_node *prev;
} list_node;

typedef size_t block_header;
typedef size_t block_footer;

// start of linked list
list_node* head = NULL;

size_t multiplier = 2;

// adds to the linked list
void linked_list_add(list_node* new_head){
    if(head == NULL){
        head = new_head;
        head->next = NULL;
        head->prev = NULL;
    }
    else{
        head->prev = new_head;
        new_head->next = head;
        head = new_head;
        head->prev = NULL;
    }
}

// removes from the linked list
void linked_list_remove(list_node* node){
    list_node* node_prev = node->prev;
    list_node* node_next = node->next;

    // if node == head then the head is now equal to the node
    if(node == head) {
        head = node_next;
    }

    // if prev != null then next of prev is next of node
    if(node_prev != NULL){
        node_prev->next = node_next;
    }

    // if next != null then prev of next is prev of node
    if(node_next != NULL){
        node_next->prev = node_prev;
    }

    return;
}

// finds a node in the linked list 
list_node* linked_list_find(size_t requested_size){
    list_node* current_node;
    current_node = head;

    while(current_node != NULL){
        // Is the current block big enough? (Check this block's header)
        if(GET_SIZE(HDRP((void*)(current_node))) >= requested_size) {
            // Remove the current block from the free list
            linked_list_remove(current_node);

            // Return the found block
            return current_node;
        }

        // Didn't find a big enough block, keep looking
        current_node = current_node->next;
    }

    // If we didn't find a sufficiently-large free block, return 'NULL'
    return NULL;
}

/* Request more memory by calling mem_map
 * Initialize the new chunk of memory as applicable
 * Update free list if applicable
 */
static void* extend(size_t s){
    size_t requested_size = multiplier * PAGE_ALIGN(s+(OVERHEAD * 2));
    if(multiplier < 16)
        multiplier++;
    void* new_page = mem_map(requested_size);

    size_t* page_words = (size_t*)new_page;
    size_t num_words = requested_size / sizeof(size_t);

    // Page prolog and terminator
    page_words[0] = 0x1;
    page_words[1] = PACK(BLOCK_OVERHEAD, 1);
    page_words[2] = PACK(BLOCK_OVERHEAD, 1);
    page_words[num_words - 1] = PACK(0, 1);

    // The new free block is the page body.
    // Set the new free block's header and footer:
    size_t free_block_size = requested_size - 4 * sizeof(size_t);
    page_words[3] = PACK(free_block_size, 0);
    page_words[num_words - 2] = PACK(free_block_size, 0);

    return (void*)&page_words[4];
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    multiplier = 2;
    head = NULL;

    extend(4090);
    return 0;
}

/* Set a block to allocated
 * Update block headers/footers as needed
 * Update free list if applicable
 * Split block if applicable
 */
static void* set_allocated(void* b, size_t size){
    //size_t* page_words = (size_t*)b;
    //page_words[1] = PACK(size, 1);

    size_t prev_size = GET_SIZE(HDRP(b));
    size_t left_over = prev_size - size;

    //list_node* prev_node = ((list_node*)(b))->prev;
    //list_node* next_node = ((list_node*)(b))->next;
    
    linked_list_remove((list_node*)(b));

    // don't split
    if(left_over < OVERHEAD + sizeof(list_node)){
        PUT(HDRP(b), PACK(GET_SIZE(HDRP(b)), 1));
        PUT(FTRP(b), PACK(GET_SIZE(FTRP(b)), 1));
    }
    else{
        PUT(HDRP(b),PACK(size, 1));
        PUT(FTRP(b),PACK(size, 1));

        void* new_header = NEXT_BLKP(b);
        PUT(HDRP(new_header), PACK(left_over, 0));
        PUT(FTRP(new_header), PACK(left_over, 0));

        linked_list_add((list_node*)(new_header));
    }

    return b;
}

/* 
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
    size_t maxsize = MAX(size, sizeof(list_node));
    // might have to change to size_t
    size_t newsize = ALIGN(maxsize + BLOCK_OVERHEAD);

    list_node* block;

    block = linked_list_find(newsize);

    // couldn't find anything of size
    if(block == NULL){
        // need a new page to allocate
        block = extend(newsize);
        set_allocated(block, newsize);
    }
    else{
        block = set_allocated((void*)(block), newsize);
    }
    
    return (void*)(block);
}

/* Coalesce a free block if applicable
 * Returns pointer to new coalesced block
 */
static void* coalesce(void* bp){
    // 4 cases for coalescing
    // First case is when neither of its neighbors are free. So we just set the allocated bit to zero
    // Second case is when the right neighbor is free. We combine both blocks into one and adjust the header and footer
    // Third case is when the left neighbor is free. We update the left block's header and the current block's footer
    // Fourth case is when the left and right blocks are free. We combine all three blocks and update the left block's header
    // and the right block's footer

    size_t ptr_size = GET_SIZE(HDRP(bp));
    
    void* new_block = bp;

    void* left_neighbor = PREV_BLKP(bp);
    void* right_neighbor = NEXT_BLKP(bp);

    size_t left_neighbor_alloc = GET_ALLOC(HDRP(left_neighbor));
    size_t right_neighbor_alloc = GET_ALLOC(HDRP(right_neighbor));

    // When both neighbors are free
    if(!left_neighbor_alloc && !right_neighbor_alloc){
        size_t new_size = GET_SIZE(HDRP(left_neighbor)) + ptr_size + GET_SIZE(HDRP(right_neighbor));
        linked_list_remove((list_node*)(right_neighbor));
        PUT(HDRP(left_neighbor), PACK(new_size, 0));
        PUT(FTRP(left_neighbor), PACK(new_size, 0));
        new_block = left_neighbor;
    }
    // When the left neigbor is free
    else if(!left_neighbor_alloc && right_neighbor_alloc){
        size_t new_size = GET_SIZE(HDRP(left_neighbor)) + ptr_size;
        PUT(HDRP(left_neighbor), PACK(new_size, 0));
        PUT(FTRP(left_neighbor), PACK(new_size, 0));
        new_block = left_neighbor;
    }
    // When the right neighbor is free
    else if(left_neighbor_alloc && !right_neighbor_alloc){
        size_t new_size = ptr_size + GET_SIZE(HDRP(right_neighbor));
        linked_list_remove((list_node*)(right_neighbor));
        linked_list_add((list_node*)(bp));
        PUT(HDRP(bp), PACK(new_size, 0));
        PUT(FTRP(bp), PACK(new_size, 0));
    }
    // When neither neighbor is free
    else{
        PUT(HDRP(bp), PACK(ptr_size, 0));
        PUT(FTRP(bp), PACK(ptr_size, 0));

        linked_list_add((list_node*)(bp));
    }

    return new_block;
}

// Checks if you can unmap the page
static void unmap_page(void* ptr){
    void* left_neighbor = HDRP(PREV_BLKP(ptr));
    void* right_neighbor = HDRP(NEXT_BLKP(ptr));

    // check if the size of the previous is overhead and the size of the next is 0
    if(GET_SIZE(left_neighbor) == OVERHEAD && GET_SIZE(right_neighbor) == 0){
        size_t page_size = PAGE_ALIGN(GET_SIZE(HDRP(ptr)));
        linked_list_remove((list_node*)(ptr));
        mem_unmap((ptr-BLOCK_OVERHEAD*2), page_size);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    ptr = coalesce(ptr);
    unmap_page(ptr);
}